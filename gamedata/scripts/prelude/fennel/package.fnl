(var {: iter-map
      : iter-filter}
     (require :prelude/fennel/iterator))

(var {: file-not-empty?
      : flist->iter
      : format-compiler-extensions}
     (require :prelude/fennel/file))

(var {: strip-extension
      : path-includes?}
     (require :prelude/fennel/path))

(λ backslashes->slashes [s]
  "Replace backslashes with slashes in string `s`."
  (s:gsub "\\" "/"))

(λ strip-/init [s]
  "Remove any `/init` suffix that may be present on `s`"
  (s:gsub "/init$" ""))

(λ path->package [s]
  "Convert string path `s` into a package name."
  (-> s
      (strip-extension)
      (backslashes->slashes)
      (strip-/init)))

(λ file->package [file]
  "Retrieve the package name for file handle `file`."
  (-> file
      (: :NameShort)
      (path->package)))

(λ iter-files [fs path extensions ?flags]
  (var flags (or ?flags 0))
  (set flags (bor FS.FS_ListFiles flags))
  (flist->iter (fs:file_list_open_ex path flags extensions)))

(λ iter-packages []
  "Produce an iterator over all script packages in the filesystem."
  (->> (iter-files (getFS)
                   "$game_scripts$"
                   (format-compiler-extensions)
                   FS.FS_ListFiles)
       (iter-filter file-not-empty?)
       (iter-map file->package)))

(λ iter-package-path [path]
  "Produce an iterator over packages included in the unix-style path `path`."
  (->> (iter-packages)
       (iter-filter (path-includes? path))))

{: backslashes->slashes
 : strip-/init
 : path->package
 : file->package
 : iter-files
 : iter-packages
 : iter-package-path}
