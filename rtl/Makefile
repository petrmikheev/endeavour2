
.PHONY: help
help:
	@echo "Usage:"
	@echo "    make generate           - generate verilog/EndeavourSoc.v from src/ (SpinalHDL)"
	@echo "    make runsim             - run simulation src/main/scala/endeavour2/sim.scala"
	@echo "    make build_2a           - run 'efx_run --flow compile' in endeavour2a"
	@echo "    make write_2a           - program endeavour2a via JTAG"
	@echo "    make build_2b           - run 'efx_run --flow compile' in endeavour2b"
	@echo "    make write_2b           - program endeavour2b via JTAG"

.PHONY: generate
generate:
	sbt 'runMain endeavour2.EndeavourSoc'

.PHONY: runsim
runsim:
	sbt 'runMain endeavour2.EndeavourSocSim'

.PHONY: build_2a
build_2a:
	@test -f endeavour2a/ip/Ddr3Controller/Ddr3Controller.v || (echo "Ddr3Controller genfiles are missing.\nRun efinity, right click on 'IP: Ddr3Controller' and choose 'Generate'." ; exit 1)
	cd endeavour2a && efx_run endeavour2a.xml --flow compile

.PHONY: write_2a
write_2a:
	cd endeavour2a && efx_run endeavour2a.xml --flow program --pgm_opts mode=jtag

.PHONY: build_2b
build_2b:
	@test -f endeavour2b/ip/Ddr3Controller/Ddr3Controller.v || (echo "Ddr3Controller genfiles are missing.\nRun efinity, right click on 'IP: Ddr3Controller' and choose 'Generate'." ; exit 1)
	cd endeavour2b && efx_run endeavour2b.xml --flow compile

.PHONY: write_2b
write_2b:
	cd endeavour2b && efx_run endeavour2b.xml --flow program --pgm_opts mode=jtag
