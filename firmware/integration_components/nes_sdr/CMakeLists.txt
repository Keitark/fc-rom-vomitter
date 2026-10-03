set(nes_sdr_root "${NES_SDR_SOURCE_ROOT}/firmware")
idf_component_register(
    SRCS
        "${nes_sdr_root}/src/nes_sdr_frame.c"
        "${nes_sdr_root}/src/nes_sdr_spc1.c"
        "${nes_sdr_root}/src/nes_sdr_live.c"
    INCLUDE_DIRS "${nes_sdr_root}/include"
)
