import { useNotifyStore, ToastLevel } from "../store/notifyStore";
import "./Toaster.css";

const LEVEL_CLASS: Record<ToastLevel, string> = {
  error: "toast--error",
  info: "toast--info",
  success: "toast--success",
};

// Bottom-right toast stack. Mounted once at the App root. Each toast auto-
// dismisses after its ttl (handled in notifyStore); the user can also click
// the × to dismiss early.
export default function Toaster() {
  const toasts = useNotifyStore((s) => s.toasts);
  const dismiss = useNotifyStore((s) => s.dismiss);

  if (toasts.length === 0) return null;

  return (
    <div className="toaster">
      {toasts.map((t) => (
        <div key={t.id} className={`toast ${LEVEL_CLASS[t.level]}`}>
          <span className="toast-msg">{t.message}</span>
          <button className="toast-close" onClick={() => dismiss(t.id)} title="Dismiss">×</button>
        </div>
      ))}
    </div>
  );
}
