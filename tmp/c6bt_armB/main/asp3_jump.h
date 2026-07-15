#ifndef ASP3_JUMP_H
#define ASP3_JUMP_H

/*
 *  実施33のasp3_jump_now()と同じ形状のジャンプ機構を，NuttXカーネル
 *  スレッドの代わりにESP-IDFネイティブアプリ（Arduino代替．shim無し
 *  ＝ground truth側）から呼び出すための移植版．docs/wifi-shim-c6.md
 *  実施33参照．
 */
void asp3_jump_now(void);

#endif /* ASP3_JUMP_H */
