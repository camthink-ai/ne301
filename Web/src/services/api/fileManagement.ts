import request from "../request";

export interface FileEntry {
  name: string;
  short_name?: string; // 8.3 short name for SD card navigation
  type: "file" | "dir";
  size: number;
  mtime: number; // unix timestamp
}

export interface FileListResponse {
  fs: string;
  path: string;
  entries: FileEntry[];
  count: number;
  error?: string;
}

export interface FileUploadResponse {
  fs: string;
  path: string;
  filename: string;
  size: number;
}

export interface FileDeleteResponse {
  fs: string;
  path: string;
  filename: string;
  deleted: boolean;
}

export interface FileRenameResponse {
  fs: string;
  old_path: string;
  new_path: string;
}

export interface FilePreviewResponse {
  filename: string;
  mime_type: string;
  size: number;
  content?: string;
  too_large?: boolean;
  file_size?: number;
  max_preview_size?: number;
}

export interface FileEditResponse {
  fs: string;
  path: string;
  size: number;
}

export type FsType = "flash" | "sd";

const fileManagement = {
  /** List directory contents */
  listDir: (fs: FsType, dirPath: string = "/") => request.get("/api/v1/files/list", {
      params: { fs, path: dirPath },
    }),

  /** Download a file - returns blob with progress callback */
  download: (fs: FsType, filePath: string, onProgress?: (pct: number) => void) => request.get("/api/v1/files/download", {
      params: { fs, path: filePath },
      responseType: "blob",
      onDownloadProgress: (e: any) => {
        if (e.total && onProgress) onProgress(Math.round((e.loaded * 100) / e.total));
      },
    }),

  /** Upload a file with progress callback */
  upload: (fs: FsType, dirPath: string, file: File, onProgress?: (pct: number) => void) => request.post("/api/v1/files/upload", file, {
      params: { fs, path: dirPath, filename: file.name },
      headers: { "Content-Type": "application/octet-stream" },
      onUploadProgress: (e: any) => {
        if (e.total && onProgress) onProgress(Math.round((e.loaded * 100) / e.total));
      },
    }),

  /** Delete a file or directory (set recursive=true for non-empty dirs) */
  deleteFile: (fs: FsType, filePath: string, recursive: boolean = false) => request.delete("/api/v1/files", {
      params: { fs, path: filePath, ...(recursive ? { recursive: 'true' } : {}) },
    }),

  /** Rename a file */
  rename: (fs: FsType, oldPath: string, newPath: string) => request.put("/api/v1/files/rename", {
      fs,
      old_path: oldPath,
      new_path: newPath,
    }),

  /** Preview a file (text/json content or image binary) */
  preview: (fs: FsType, filePath: string, asImage: boolean = false) => request.get("/api/v1/files/preview", {
      params: { fs, path: filePath },
      ...(asImage ? { responseType: "blob" } : {}),
    }),

  /** Edit a text file */
  edit: (fs: FsType, filePath: string, content: string) => request.put("/api/v1/files/edit", {
      fs,
      path: filePath,
      content,
    }),

  /** Create a directory or empty file */
  create: (fs: FsType, parentPath: string, name: string, type: "file" | "dir") => request.post("/api/v1/files/create", {
      fs,
      path: parentPath,
      name,
      type,
    }),
};

export default fileManagement;
