#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 播放单个音符
void BuzzerPlayTone(unsigned freq_hz, unsigned duration_ms);
// 播放音符序列（阻塞）
void BuzzerPlaySequence(const unsigned* freq_hz, const unsigned* duration_ms, unsigned count);

void RunBootBuzzerTest();

#ifdef __cplusplus
}
#endif
