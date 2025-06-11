-- Given a named set of tests, run them and report their results
function run_tests(name, tests)
   print("+ [TEST] " .. name)

   local passes = {}
   local fails = {}

   for k,v in pairs(tests) do
      local res, err = v()
      if res then
         table.insert(passes, k)
      else
         table.insert(fails, k .. ": " .. err)
      end
   end

   for _,v in ipairs(passes) do
      print("- [PASS] " .. v)
   end

   for _,v in ipairs(fails) do
      print("! [FAIL] " .. v)
   end
end

return {
   run_tests = run_tests,
}
