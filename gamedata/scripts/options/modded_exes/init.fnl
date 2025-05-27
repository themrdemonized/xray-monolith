(var {: group} (require :options/builder))

(group :id :modded_exes
       (require (.. _PACKAGE "/visual"))
       (require (.. _PACKAGE "/sound"))
       (require (.. _PACKAGE "/control"))
       (require (.. _PACKAGE "/gameplay"))
       (require (.. _PACKAGE "/saves"))
       (require (.. _PACKAGE "/debug")))
