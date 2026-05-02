import * as exposes from 'zigbee-herdsman-converters/lib/exposes';
import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

const e = exposes.presets;
const ea = exposes.access;

const waterTotalByEndpoint = {
    31: 'zone_1_total_water',
    32: 'zone_2_total_water',
    33: 'zone_3_total_water',
    34: 'zone_4_total_water',
    35: 'zone_5_total_water',
};

const analogInputByEndpoint = {
    ...waterTotalByEndpoint,
    20: 'active_zone',
    21: 'max_charging_power_today',
    22: 'daily_solar_generation',
    23: 'daily_power_consumption',
    24: 'battery_voltage',
    25: 'pv_voltage',
    26: 'pv_power',
    27: 'controller_temperature',
    28: 'water_level',
    41: 'fault_code',
    42: 'charging_status',
    43: 'waterer_state',
};

const durationByEndpoint = {
    10: 'duration_zone_1',
    11: 'duration_zone_2',
    12: 'duration_zone_3',
    13: 'duration_zone_4',
    14: 'duration_zone_5',
};

const durationEndpointByKey = Object.fromEntries(
    Object.entries(durationByEndpoint).map(([endpoint, key]) => [key, Number(endpoint)]),
);

const durationEndpointByName = {
    zone_1: 10,
    zone_2: 11,
    zone_3: 12,
    zone_4: 13,
    zone_5: 14,
};

const durationMinSec = 1;
const durationMaxSec = 30 * 60;

const faultCodes = {
    0: 'none',
    1: 'battery_low',
    2: 'water_low',
    3: 'prime_timeout',
    4: 'max_duration',
    5: 'invalid_request',
    6: 'load_enable_failed',
    7: 'stale_data',
};

const chargingStatuses = {
    0: 'not_charging',
    1: 'startup',
    2: 'mppt',
    3: 'equalisation',
    4: 'boost',
    5: 'float',
    6: 'current_limiting',
};

const watererStates = {
    0: 'idle',
    1: 'priming',
    2: 'watering',
    3: 'fault',
};

const activeZones = {
    0: 'none',
    1: 'zone_1',
    2: 'zone_2',
    3: 'zone_3',
    4: 'zone_4',
    5: 'zone_5',
};

const haNamesByObjectSuffix = {
    pv_power: 'PV power',
    max_charging_power_today: 'Max charging power today',
    daily_solar_generation: 'Daily solar generation',
    daily_power_consumption: 'Daily power consumption',
};

const waterTotalHaNamesByObjectSuffix = {
    zone_1_total_water: 'Zone 1 total water',
    zone_2_total_water: 'Zone 2 total water',
    zone_3_total_water: 'Zone 3 total water',
    zone_4_total_water: 'Zone 4 total water',
    zone_5_total_water: 'Zone 5 total water',
};

function lookupEnumValue(map, value) {
    const key = Math.trunc(Number(value));
    return map[key] ?? 'unknown';
}

function clampDurationSec(value) {
    const numeric = Math.round(Number(value));
    if (!Number.isFinite(numeric)) {
        return durationMinSec;
    }
    return Math.min(durationMaxSec, Math.max(durationMinSec, numeric));
}

async function safeRead(endpoint, cluster, attributes) {
    if (!endpoint) {
        return;
    }

    try {
        await endpoint.read(cluster, attributes);
    } catch {
        // Best-effort initialization only. Live reports continue to work even
        // when a single read fails during configure/reconfigure.
    }
}

const fromZigbee = [
    {
        cluster: 'genAnalogInput',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            const name = analogInputByEndpoint[msg.endpoint.ID];
            if (!name || msg.data.presentValue === undefined) {
                return {};
            }

            if (name === 'fault_code') {
                return {[name]: lookupEnumValue(faultCodes, msg.data.presentValue)};
            }

            if (name === 'charging_status') {
                const result = {[name]: lookupEnumValue(chargingStatuses, msg.data.presentValue)};
                if (meta?.state?.fault_code == null) {
                    result.fault_code = 'none';
                }
                return result;
            }

            if (name === 'waterer_state') {
                return {[name]: lookupEnumValue(watererStates, msg.data.presentValue)};
            }

            if (name === 'active_zone') {
                return {[name]: lookupEnumValue(activeZones, msg.data.presentValue)};
            }

            return {[name]: msg.data.presentValue};
        },
    },
    {
        cluster: 'genAnalogOutput',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg) => {
            const name = durationByEndpoint[msg.endpoint.ID];
            if (!name || msg.data.presentValue === undefined) {
                return {};
            }

            return {[name]: clampDurationSec(msg.data.presentValue)};
        },
    },
    {
        cluster: 'genPowerCfg',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg) => {
            const result = {};
            if (msg.data.batteryPercentageRemaining !== undefined) {
                result.battery = msg.data.batteryPercentageRemaining / 2;
            }
            if (msg.data.batteryVoltage !== undefined) {
                result.battery_voltage = msg.data.batteryVoltage / 10;
            }
            return result;
        },
    },
];

const toZigbee = [
    {
        key: ['duration', ...Object.keys(durationEndpointByKey)],
        convertSet: async (entity, key, value, meta) => {
            const durationSec = clampDurationSec(value);
            let endpoint = null;

            if (durationEndpointByKey[key] !== undefined) {
                endpoint = meta.device.getEndpoint(durationEndpointByKey[key]);
            } else if (key === 'duration') {
                if (durationByEndpoint[entity?.ID]) {
                    endpoint = entity;
                } else if (durationEndpointByName[meta?.endpoint_name] !== undefined) {
                    endpoint = meta.device.getEndpoint(durationEndpointByName[meta.endpoint_name]);
                }
            }

            if (!endpoint) {
                throw new Error(`No duration endpoint for ${key}`);
            }

            await endpoint.write('genAnalogOutput', {presentValue: durationSec});
            return {state: {duration: durationSec}};
        },
    },
    {
        key: ['clear_fault'],
        convertSet: async (entity, key, value, meta) => {
            const endpoint = meta.device.getEndpoint(43);
            await endpoint.command('genOnOff', 'on', {});
        },
    },
];

export default {
    zigbeeModel: ['solar-plant-waterer'],
    model: 'solar-plant-waterer',
    vendor: 'Ivanbuilds',
    description: 'Solar plant watering controller',
    configure: async (device) => {
        const batteryEp = device.getEndpoint(20);
        await safeRead(batteryEp, 'genPowerCfg', ['batteryPercentageRemaining', 'batteryVoltage']);
        await safeRead(batteryEp, 'genAnalogInput', ['presentValue']);

        for (const endpointId of Object.keys(analogInputByEndpoint).map(Number)) {
            if (endpointId === 20) {
                continue;
            }
            await safeRead(device.getEndpoint(endpointId), 'genAnalogInput', ['presentValue']);
        }

        for (const endpointId of Object.keys(durationByEndpoint).map(Number)) {
            const endpoint = device.getEndpoint(endpointId);
            await safeRead(endpoint, 'genOnOff', ['onOff']);
            await safeRead(endpoint, 'genAnalogOutput', ['presentValue']);
        }

        await safeRead(device.getEndpoint(43), 'genOnOff', ['onOff']);
    },
    fromZigbee,
    toZigbee,
    extend: [
        m.deviceEndpoints({
            endpoints: {
                zone_1: 10,
                zone_2: 11,
                zone_3: 12,
                zone_4: 13,
                zone_5: 14,
                clear_fault: 43,
            },
        }),
        m.onOff({
            endpointNames: ['zone_1', 'zone_2', 'zone_3', 'zone_4', 'zone_5'],
            powerOnBehavior: false,
        }),
    ],
    exposes: [
        e.battery(),
        e.numeric('battery_voltage', ea.STATE).withLabel('Battery voltage').withUnit('V'),
        e.numeric('pv_voltage', ea.STATE).withLabel('PV voltage').withUnit('V'),
        e.numeric('pv_power', ea.STATE).withLabel('PV power').withUnit('W'),
        e.numeric('controller_temperature', ea.STATE).withLabel('Controller temperature').withUnit('C'),
        e.numeric('water_level', ea.STATE).withLabel('Water level').withUnit('%'),
        e.numeric('max_charging_power_today', ea.STATE).withLabel('Max charging power today').withUnit('W'),
        e.numeric('daily_solar_generation', ea.STATE).withLabel('Daily solar generation').withUnit('Wh'),
        e.numeric('daily_power_consumption', ea.STATE).withLabel('Daily power consumption').withUnit('Wh'),
        e.enum('fault_code', ea.STATE, [...Object.values(faultCodes), 'unknown'])
            .withLabel('Fault code')
            .withDescription('Current watering controller fault state'),
        e.enum('charging_status', ea.STATE, [...Object.values(chargingStatuses), 'unknown'])
            .withLabel('Charging status')
            .withDescription('Solar charge controller charging stage'),
        e.enum('waterer_state', ea.STATE, [...Object.values(watererStates), 'unknown'])
            .withLabel('Waterer state')
            .withDescription('Aggregate watering controller state for automation guards'),
        e.enum('active_zone', ea.STATE, [...Object.values(activeZones), 'unknown'])
            .withLabel('Active zone')
            .withDescription('Zone currently owned by the watering state machine'),
        e.numeric('zone_1_total_water', ea.STATE).withLabel('Zone 1 total water').withUnit('L')
            .withDescription('Lifetime water dispensed by zone 1'),
        e.numeric('zone_2_total_water', ea.STATE).withLabel('Zone 2 total water').withUnit('L')
            .withDescription('Lifetime water dispensed by zone 2'),
        e.numeric('zone_3_total_water', ea.STATE).withLabel('Zone 3 total water').withUnit('L')
            .withDescription('Lifetime water dispensed by zone 3'),
        e.numeric('zone_4_total_water', ea.STATE).withLabel('Zone 4 total water').withUnit('L')
            .withDescription('Lifetime water dispensed by zone 4'),
        e.numeric('zone_5_total_water', ea.STATE).withLabel('Zone 5 total water').withUnit('L')
            .withDescription('Lifetime water dispensed by zone 5'),
        e.numeric('duration_zone_1', ea.ALL).withLabel('Zone 1 duration').withUnit('s')
            .withValueMin(durationMinSec).withValueMax(durationMaxSec).withValueStep(1),
        e.numeric('duration_zone_2', ea.ALL).withLabel('Zone 2 duration').withUnit('s')
            .withValueMin(durationMinSec).withValueMax(durationMaxSec).withValueStep(1),
        e.numeric('duration_zone_3', ea.ALL).withLabel('Zone 3 duration').withUnit('s')
            .withValueMin(durationMinSec).withValueMax(durationMaxSec).withValueStep(1),
        e.numeric('duration_zone_4', ea.ALL).withLabel('Zone 4 duration').withUnit('s')
            .withValueMin(durationMinSec).withValueMax(durationMaxSec).withValueStep(1),
        e.numeric('duration_zone_5', ea.ALL).withLabel('Zone 5 duration').withUnit('s')
            .withValueMin(durationMinSec).withValueMax(durationMaxSec).withValueStep(1),
        e.enum('clear_fault', ea.SET, ['clear'])
            .withLabel('Clear fault')
            .withDescription('Clear the latched watering fault after it has been acknowledged'),
    ],
    meta: {
        multiEndpoint: true,
        overrideHaDiscoveryPayload: (payload) => {
            for (const [suffix, name] of Object.entries(waterTotalHaNamesByObjectSuffix)) {
                if (payload.object_id?.endsWith(`_${suffix}`)) {
                    payload.name = name;
                    payload.device_class = 'water';
                    payload.state_class = 'total_increasing';
                    payload.unit_of_measurement = 'L';
                    return;
                }
            }

            for (const [suffix, name] of Object.entries(haNamesByObjectSuffix)) {
                if (payload.object_id?.endsWith(`_${suffix}`)) {
                    payload.name = name;
                    return;
                }
            }
        },
    },
};
