import { useState, ReactNode } from "react";
import "./BottomTabs.css";

interface Tab {
  id: string;
  label: string;
  content: ReactNode;
}

interface Props {
  tabs: Tab[];
  defaultTab?: string;
}

export default function BottomTabs({ tabs, defaultTab }: Props) {
  const [active, setActive] = useState(defaultTab ?? tabs[0]?.id ?? "");

  return (
    <div className="bottom-tabs">
      <div className="bt-tab-bar">
        {tabs.map((t) => (
          <button
            key={t.id}
            className={`bt-tab ${t.id === active ? "bt-tab--active" : ""}`}
            onClick={() => setActive(t.id)}
          >
            {t.label}
          </button>
        ))}
      </div>
      <div className="bt-content">
        {tabs.find((t) => t.id === active)?.content}
      </div>
    </div>
  );
}
