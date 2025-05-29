(var {: iter-map
      : iter-filter}
     (require :prelude/fennel/iterator))

(var {: file-not-empty?
      : flist->iter}
     (require :prelude/fennel/file))

(var {:PATTERN_FILE_PATH PATTERN-FILE-PATH}
     (require :scam/compiler))

(λ strip-extension [s]
  "Remove any file extension from path `s`."
  (var (path name _) (s:match PATTERN-FILE-PATH))
  (.. path name))

(λ backslashes->slashes [s]
  "Replace backslashes with slashes in string `s`."
  (s:gsub "\\" "/"))

(λ strip-/init [s]
  "Remove any `/init` suffix that may be present on `s`"
  (s:gsub "/init$" ""))

(λ path->package [s]
  (-> s
      (strip-extension)
      (backslashes->slashes)
      (strip-/init)))

(λ file->package [file]
  (-> file
      (: :NameShort)
      (path->package)))

(λ iter-packages [fs path ?extensions ?flags]
  (var flags (or ?flags 0))
  (set flags (bor FS.FS_ListFiles flags))
  
  (var flist (fs:file_list_open_ex path flags ?extensions))

  (->> flist
       (flist->iter)
       (iter-filter file-not-empty?)
       (iter-map file->package)))

{: path->package
 : file->package
 : iter-packages}
