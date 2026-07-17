import React from "react";
import ReactDOM from "react-dom/client";
import { injectTheme } from "./theme";
import { rpc } from "./rpc";
import { useProjectStore } from "./store/projectStore";
import { useTransportStore } from "./store/transportStore";
import { useMeterStore } from "./store/meterStore";
import { TransportSnapshot, MetersPayload } from "./rpc/types";
import App from "./App";

injectTheme();

const cleanups: (() => void)[] = [];

function setupSubscriptions() {
  for (const c of cleanups) c();
  cleanups.length = 0;

  cleanups.push(rpc.onNotification("notify.transport", (_, params) => {
    useTransportStore.getState().update(params as TransportSnapshot);
  }));

  cleanups.push(rpc.onNotification("notify.meters", (_, params) => {
    useMeterStore.getState().update(params as MetersPayload);
  }));

  cleanups.push(rpc.onNotification("notify.treeChanged", () => {
    useProjectStore.getState().syncSnapshot(rpc).catch(() => {});
  }));
}

async function init() {
  await rpc.connect();
  setupSubscriptions();

  await useProjectStore.getState().syncSnapshot(rpc);

  ReactDOM.createRoot(document.getElementById("root")!).render(
    <React.StrictMode>
      <App />
    </React.StrictMode>
  );
}

init().catch(console.error);
