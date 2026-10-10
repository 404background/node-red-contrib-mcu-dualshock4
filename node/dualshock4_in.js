/* Editor-side definition only: the node runs on the MCU (dualshock4_in_mcu.js) */
module.exports = function (RED) {
    function DualShock4InNode(config) {
        RED.nodes.createNode(this, config);
    }

    RED.nodes.registerType("dualshock4_in", DualShock4InNode);
};
