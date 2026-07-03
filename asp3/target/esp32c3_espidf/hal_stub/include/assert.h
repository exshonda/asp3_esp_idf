/*
 *  esp-halヘッダ用のassert.hスタブ
 *
 *  ツールチェーン（riscv64-unknown-elf-gcc）にnewlibヘッダが無い環境の
 *  ためのスタブ．ASP3カーネルはlibcヘッダを使わない（自己完結）が，
 *  esp-halのesp_assert.h／hal/assert.hがassert.hを要求する．
 *  HAL_ASSERTは無効化する（LL層はassertを実質使用しない）．
 */
#ifndef TOPPERS_HAL_STUB_ASSERT_H
#define TOPPERS_HAL_STUB_ASSERT_H

#define assert(x) ((void)0)

#endif /* TOPPERS_HAL_STUB_ASSERT_H */
