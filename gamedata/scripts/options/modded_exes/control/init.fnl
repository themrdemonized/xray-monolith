(var {: group} (require :options/builder))

(group :id :control 
       (require (.. _PACKAGE :/keyboard))
       (require (.. _PACKAGE :/mouse))
       (require (.. _PACKAGE :/camera))
       (require (.. _PACKAGE :/pda)))
