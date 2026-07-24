// frontend/src/components/LoadingOverlay.tsx
import { useProjectStore } from "../store/projectStore";
import "./LoadingOverlay.css";

export function LoadingOverlay() {
  const loadingProject = useProjectStore((s) => s.loadingProject);
  const loadProgress = useProjectStore((s) => s.loadProgress);

  if (!loadingProject) return null;

  return (
    <div className="loading-overlay">
      <div className="loading-overlay__card">
        <div className="loading-overlay__spinner" />
        <div className="loading-overlay__text">
          {loadProgress?.message ?? "Loading project..."}
        </div>
        {loadProgress && loadProgress.percent > 0 && loadProgress.percent < 1 && (
          <div className="loading-overlay__bar-track">
            <div
              className="loading-overlay__bar-fill"
              style={{ width: `${Math.round(loadProgress.percent * 100)}%` }}
            />
          </div>
        )}
      </div>
    </div>
  );
}