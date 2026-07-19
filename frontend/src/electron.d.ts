export {};

interface DirEntry {
  name: string;
  isDir: boolean;
  path: string;
}

declare global {
  interface Window {
    hdaw?: {
      showOpenDialog: (options: any) => Promise<{ canceled: boolean; filePaths: string[] }>;
      showSaveDialog: (options: any) => Promise<{ canceled: boolean; filePath: string }>;
      readDirectory: (dirPath: string) => Promise<DirEntry[]>;
    };
  }
}
