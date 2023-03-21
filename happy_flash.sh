#!/bin/bash
#emerge-geralt chromeos-zephyr  coreboot depthcharge libpayload chromeos-bootimage
:<<EOF
TODO: read function
#sudo flashrom -p raiden_debug_spi:target=AP -r /tmp/new_bios.bin
#sudo flashrom -p host -r /tmp/new_bios.bin
#cbfstool image.bin layout
EOF

# if [ "$1 " == "debug_ec "]; then
#     zmake build geralt &&
#     flash_ec --board=geralt --image /mnt/host/source/src/platform/ec/build/zephyr/demo/output/ec.bin || exit 1
#     exit 0
# fi

flash_bios(){
    bios=$1
    PORT="${2-9999}"
    serial="$(dut-control -p "${PORT}" serialname | cut -d: -f2)"
    # if micro or ccd are run through v4, then getting the serial has a different
    # dut-control command
    if [[ $(dut-control -p "${PORT}" servo_type) == *"servo_v4_with_servo"* ]]; then
        serial="$(dut-control -p "${PORT}" servo_micro_serialname | cut -d: -f2)"
    elif [[ $(dut-control -p "${PORT}" servo_type) == *"servo_v4_with_ccd_cr50"* ]]; then
        serial="$(dut-control -p "${PORT}" ccd_serialname | cut -d: -f2)"
    fi

    if [[ $(dut-control -p "${PORT}" servo_type) == *"servo_micro"* ]]; then
        dut-control ec_uart_cmd:apshutdown
        dut-control cpu_fw_spi:on fw_wp_en:off
    #  dut-control spi2_vref:pp3300 spi2_buf_en:on spi2_buf_on_flex_en:on spi_hold:off
        sleep 1
        sudo flashrom -p raiden_debug_spi -w ${bios}
        sleep 1
        dut-control cpu_fw_spi:off
        sleep 1 
        dut-control ec_uart_cmd:powerbtn
    #   dut-control spi2_vref:off spi2_buf_en:off spi2_buf_on_flex_en:off spi_hold:off
    elif [[ $(dut-control -p "${PORT}" servo_type) == *"ccd_cr50"* ]]; then
    # To detect suzy or v4 ccd
    sudo flashrom -n -p raiden_debug_spi:serial="${serial}",target=AP --noverify -w "${bios}" -V
    else
    dut-control -p "${PORT}" cold_reset:on spi2_buf_en:on spi2_vref:pp1800 spi2_buf_on_flex_en:on
    sudo flashrom -p ft2232_spi:type=servo-v2,serial="${serial}" -w "${bios}" -V
    dut-control -p "${PORT}" spi2_vref:off cold_reset:off spi2_buf_en:off spi2_buf_on_flex_en:off
    fi
}

opt=${1:-all}
if [[ "$opt" == "ec" || "$opt" == "all" ]]; then
    
    if [ "$2 " == " " ];then
        echo -e  "\nbuilding ec ...."
        zmake configure demo --clobber || exit 1
        echo -e "Good configure =========== \n"
        zmake -j8 build demo || exit 1
    fi
    echo -e  "\nflash ec ...."
    ec=${2:-/mnt/host/source/src/platform/ec/build/zephyr/demo/output/ec.bin}
    flash_ec --board=geralt --image ${ec} || exit 1
fi

if [[ "$opt" == "bios" || "$opt" == "all" ]]; then
    if [[ "$opt" == "all" ]]; then
        shift 1  
    fi
    echo -e  "\nflash bios ...."
    bios=${2:-image-geralt.serial.bin}
    flash_bios ${bios} && echo "Done"
fi
