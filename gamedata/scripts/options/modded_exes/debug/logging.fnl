(var {: page
      : list-bool}
     (import :/options/builder))

(page :id :logging 
      (list-bool :id :string_table_error_msg)
      (list-bool :id :log_timestamps)
      (list-bool :id :print_bone_warnings))
