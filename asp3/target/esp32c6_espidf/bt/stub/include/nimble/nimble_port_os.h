/*
 *  互換シム：controller/esp32c6/bt.c は "nimble/nimble_port_os.h" を
 *  無条件includeするが，本hal（esp-hal-3rdparty）submoduleの当該
 *  ツリーにはこのファイルが実在しない（上流ドリフト／typo）．
 *
 *  ~/tools/esp-idf-v6.1/components/bt/controller/esp32c6/bt.c（正本
 *  ESP-IDF v6.1）を確認したところ，同じ行は
 *  "nimble/nimble_port_freertos.h" をincludeしており，これは
 *  hal側にも実在する（porting/npl/freertos/include/nimble/
 *  nimble_port_freertos.h）．本ファイルはその1行差分を吸収する
 *  互換シム（hal/submoduleは編集しないため，target側で補う．
 *  詳細はdocs/ble-c5c6.md「BLE実施01」）．
 */
#ifndef TOPPERS_BT_C6_NIMBLE_PORT_OS_H
#define TOPPERS_BT_C6_NIMBLE_PORT_OS_H

#include "nimble/nimble_port_freertos.h"

#endif /* TOPPERS_BT_C6_NIMBLE_PORT_OS_H */
