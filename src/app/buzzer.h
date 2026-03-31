#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 播放单个音符
void BuzzerPlayTone(unsigned freq_hz, unsigned duration_ms);
// 播放音符序列（阻塞）
void BuzzerPlaySequence(const unsigned* freq_hz, const unsigned* duration_ms, unsigned count);
// 播放音符序列，并为每个音符指定额外停顿（freq=0 时会按 duration 静音停顿）
void BuzzerPlaySequenceWithGap(const unsigned* freq_hz, const unsigned* duration_ms,
                               const unsigned* gap_ms, unsigned count);

void RunBootBuzzerTest();

#ifdef __cplusplus
}
#endif
