/* Editor-side definition only: the node runs on the MCU (dualshock4_config_mcu.js) */
module.exports = function (RED) {
    function DualShock4ConfigNode(config) {
        RED.nodes.createNode(this, config);
    }

    RED.nodes.registerType("dualshock4_config", DualShock4ConfigNode);
};
