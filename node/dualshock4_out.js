module.exports = function (RED) {
    function DualShock4OutNode(config) {
        RED.nodes.createNode(this, config);
        const node = this;

        node.mac = config.mac || "";
        node.ds4mac = config.ds4mac || "";

        node.on("input", function (msg) {
            const payload = msg.payload;
            if (!payload || typeof payload !== "object") return;

            node.status({ fill: "green", shape: "dot", text: "sent" });
        });

        node.on("close", function (done) {
            node.status({});
            if (typeof done === "function") {
                done();
            }
        });
    }

    RED.nodes.registerType("dualshock4_out", DualShock4OutNode);
};
