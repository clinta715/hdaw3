import React from "react";
import ReactDOM from "react-dom/client";
import { injectTheme } from "./theme";
import { rpc } from "./rpc";
import { useProjectStore } from "./store/projectStore";
import { useTransportStore } from "./store/transportStore";
import { useMeterStore } from "./store/meterStore";
import App from "./App";

injectTheme();

async function init() {
  await rpc.connect();

  const poll = async () => {
    if (rpc["destroyed"]) return;
    try {
      await useProjectStore.getState().syncSnapshot(rpc);
      const transport = await rpc.call("read.transport");
      useTransportStore.getState().update(transport as any);
    } catch { /* retry next cycle */ }
    setTimeout(poll, 500);
  };
  poll();

  rpc.onNotification("notify.meters", (_, params) => {
    useMeterStore.getState().update(params as any);
  });

  ReactDOM.createRoot(document.getElementById("root")!).render(
    <React.StrictMode>
      <App />
    </React.StrictMode>
  );
}

init().catch(console.error);
