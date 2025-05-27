(var {: group} (require :options/builder))

(group :id :visual 
       (require (.. _PACKAGE :/ui_hud))
       (require (.. _PACKAGE :/crosshair))
       (require (.. _PACKAGE :/3d_scopes))
       (require (.. _PACKAGE :/particles))
       (require (.. _PACKAGE :/wallmarks))
       (require (.. _PACKAGE :/hdr10)))
