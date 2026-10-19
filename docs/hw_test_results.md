# Hardware test results

Run: 2026-10-19T22:14:45 on Nucleo-H753ZI, USART3 115200 8N1 via ST-Link VCP, `tools/hwtest.py`.

| Test | Result | Detail |
|---|---|---|
| update fast -> slow | PASS | 1516 B in 0.95 s, saw 'app slow running' |
| update slow -> fast | PASS | 1512 B in 0.98 s, saw 'app fast running' |
| flipped payload byte rejected, not booted | PASS | NACK VERIFY_FAIL=True, after reset: 'BL: start\r\nBL: no valid app, staying' |
| wrong magic rejected at START, old app intact | PASS | NACK BAD_HEADER=True, old app boots=True |
| oversized image rejected at START, old app intact | PASS | NACK BAD_HEADER=True, old app boots=True |
| bad header CRC rejected at START, old app intact | PASS | NACK BAD_HEADER=True, old app boots=True |
| transfer killed halfway, then reset: not booted | PASS | after reset: 'BL: start\r\nBL: no valid app, staying' |
| out-of-order and duplicate packets handled | PASS | duplicate DATA -> ACK; out-of-order offset -> NACK BAD_OFFSET; wrong seq -> NACK BAD_SEQ; completed update after the errors, VERIFY ok |
| throughput 32 KB image | PASS | total 4.20 s, erase 0.80 s, data 3.38 s, 9.47 KB/s |
| throughput 128 KB image | PASS | total 15.29 s, erase 1.55 s, data 13.67 s, 9.36 KB/s |
