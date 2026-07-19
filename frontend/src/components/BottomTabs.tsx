import { useState, useEffect, ReactNode } from "react";
import "./BottomTabs.css";

interface Tab {
  id: string;
  label: string;
  content: ReactNode;
}

interface Props {
  tabs: Tab[];
  defaultTab?: string;
  activeTab?: string;
  onTabChange?: (id: string) => void;
}

export default function BottomTabs({ tabs, defaultTab, activeTab, onTabChange }: Props) {
  const [internal, setInternal] = useState(defaultTab ?? tabs[0]?.id ?? "");
  const active = activeTab ?? internal;

  useEffect(() => {
    if (activeTab !== undefined && activeTab !== internal) {
      setInternal(activeTab);
    }
  }, [activeTab]);

  const setActive = (id: string) => {
    setInternal(id);
    onTabChange?.(id);
  };

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
