module.exports = function (RED) {
    function DualShock4InNode(config) {
        RED.nodes.createNode(this, config);
        const node = this;

        node.ds4mac = config.ds4mac || "";
        node.interval = config.interval || 100;
        node.selectedInputs = config.selectedInputs || {};

        node.on("input", function (msg) {
            if (msg.kill === true) {
                node.status({ fill: "red", shape: "ring", text: "stopped" });
                return;
            }

            node.status({ fill: "yellow", shape: "ring", text: "waiting" });
        });

        node.on("close", function (done) {
            node.status({});
            if (typeof done === "function") {
                done();
            }
        });
    }

    RED.nodes.registerType("dualshock4_in", DualShock4InNode);
};
