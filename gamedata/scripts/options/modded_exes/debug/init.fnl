(var {: group} (require :options/builder))

(group :id :debug 
       (require (.. _PACKAGE :/logging))
       (require (.. _PACKAGE :/metrics)))
