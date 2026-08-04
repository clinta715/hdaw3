export {};

interface DirEntry {
  name: string;
  isDir: boolean;
  path: string;
}

declare global {
  interface Window {
    HDAW_DEBUG_RENDERS?: boolean;
    __HDAW_RENDER_COUNTS?: Map<string, number>;
    __HDAW_GET_RENDER_COUNTS?: () => Record<string, number>;
    __HDAW_RESET_RENDER_COUNTS?: () => void;
    hdaw?: {
      showOpenDialog: (options: any) => Promise<{ canceled: boolean; filePaths: string[] }>;
      showSaveDialog: (options: any) => Promise<{ canceled: boolean; filePath: string }>;
      readDirectory: (dirPath: string) => Promise<DirEntry[]>;
      isDirty: () => Promise<boolean>;
      saveProject: () => Promise<void>;
      requestClose: () => Promise<void>;
      on: (channel: string, callback: (...args: unknown[]) => void) => (() => void);
    };
  }
}
