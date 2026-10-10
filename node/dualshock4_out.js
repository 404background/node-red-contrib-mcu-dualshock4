/* Editor-side definition only: the node runs on the MCU (dualshock4_out_mcu.js) */
module.exports = function (RED) {
    function DualShock4OutNode(config) {
        RED.nodes.createNode(this, config);
    }

    RED.nodes.registerType("dualshock4_out", DualShock4OutNode);
};
