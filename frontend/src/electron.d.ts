export {};

declare global {
  interface Window {
    hdaw?: {
      showOpenDialog: (options: any) => Promise<{ canceled: boolean; filePaths: string[] }>;
      showSaveDialog: (options: any) => Promise<{ canceled: boolean; filePath: string }>;
    };
  }
}
