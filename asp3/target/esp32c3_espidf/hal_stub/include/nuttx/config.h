/*
 *  esp-hal同梱のNuttX用sdkconfig.hスタブが要求するnuttx/config.hの
 *  ASP3用スタブ．NuttXのKconfig値のうち，sdkconfig.hが選択を必須と
 *  するもの（#error回避）だけを定義する．
 */
#ifndef TOPPERS_HAL_STUB_NUTTX_CONFIG_H
#define TOPPERS_HAL_STUB_NUTTX_CONFIG_H

/*  SPIフラッシュクロック（Direct Bootのボード既定＝40MHz）  */
#define CONFIG_ESPRESSIF_FLASH_FREQ_40M    1

#endif /* TOPPERS_HAL_STUB_NUTTX_CONFIG_H */
