# C++ 1BRC Interview Q&A

## 專案與架構

### 1. 這個專案解決什麼問題？

讀取十億筆 `station;temperature` 記錄，計算每個測站的最小值、平均值與最大值，最後依 UTF-8 bytes 排序輸出。專案優先順序是正確性、執行穩定性，最後才是效能。

### 2. 為什麼保留 portable baseline？

Baseline 使用 `std::getline`、標準解析與 `std::unordered_map`，容易閱讀與驗證。所有優化版本都必須和它的輸出 byte-for-byte 相同，因此它是 correctness oracle，而不是最快版本。

### 3. 主要版本是哪一個？

`onebrc_parallel` 是主要方案。它使用標準容器、固定 worker buffer 和 thread-local aggregation；1B 約 3.69 秒，peak committed memory 約 10.58 MiB。`onebrc_parallel_flat_map` 是以較高固定記憶體換取約 10% 加速的選配方案。

## 正確性與數值

### 4. 為什麼溫度使用整數？

輸入固定為一位小數，因此將 `12.3` 儲存為 `123`。這可避免浮點誤差，也讓 parsing、加總和輸出更簡單。Min/max 使用 32-bit，sum/count 使用 64-bit，避免十億筆累加溢位。

### 5. 平均值如何正確四捨五入？

使用整數運算實作 `floor(sum / count + 0.5)`，包含負數 half tie 向正無限方向的規則，不依賴浮點數或目前 locale。

### 6. 如何驗證輸出正確？

CTest 涵蓋負溫、極值、rounding、UTF-8、100-byte 名稱、CRLF/LF、無末尾換行、malformed records、tiny chunk boundaries 和 10,001 stations。完整 1B 執行也與 baseline raw stdout 完全一致。

## 記憶體與穩定性

### 7. 如何避免 `std::bad_alloc`？

程式不保存歷史 rows，也不把完整輸入載入 heap。記憶體只由固定 buffer、thread count 與最多 10,000 stations 決定；`std::bad_alloc` 會輸出固定錯誤並回傳 exit code 3。

### 8. `reserve(10'000)` 是否等於限制 station 數量？

不是。`reserve` 只降低 rehash 機率，不會阻止第 10,001 個 key。主要版本會在新 key 插入前明確檢查並受控拒絕。

### 9. 為什麼主要方案不用較快的 flat map？

Flat map 將 1B median 從 3.335 秒降至 2.995 秒，但 peak committed memory 從約 10.58 MiB 增至 39.36 MiB，且增加 probing、sentinel 與 iterator 的維護成本。對 edge-oriented 場景，約 10% 時間改善不足以優先於低記憶體與可維護性。

### 10. 如何測試記憶體不足行為？

Windows 測試使用 Job Object 限制 process memory。正式版本必須在限制內成功；測試專用的大 buffer 版本則必須可預期地捕捉 allocation failure。Job runner 使用 RAII 管理 handles，檢查 Win32 錯誤並設置 timeout。

## Parsing、I/O 與平行化

### 11. Baseline 為什麼慢？

每列經過通用字串解析、temporary substring、可能的 heap allocation、iostream 狀態處理和 node-based hash lookup。單筆差異很小，但會被十億列放大。

### 12. 為什麼 buffered I/O 比 memory mapping 快？

在這台 Windows 機器上，4 MiB buffered target 的 100M 時間約 2.79 秒，mapped target 約 3.24 秒。Mapping 並非保證更快，且整檔 mapping 曾使 file-backed working set 明顯增加，所以主要平行版本採固定大小 range reads。

### 13. 如何安全切割檔案？

先計算 nominal byte offsets，再向後尋找下一個 `\n`。每個 range 從完整 record 開始並在完整 record 後結束，因此不漏行也不重複。無末尾換行和 CRLF 都有自動測試。

### 14. 多執行緒如何避免鎖競爭？

每個 `std::jthread` 擁有自己的 buffer 與 station table，hot loop 不共享可變狀態。所有 workers join 後，主執行緒只做一次合併。

### 15. Worker 發生例外怎麼辦？

每個 worker 捕捉 `std::exception_ptr`，join 完成後由主執行緒重新拋出，再交給統一錯誤處理。這避免 exception 穿越 thread boundary 導致 `std::terminate`。

### 16. 為什麼使用 32 threads 而 CPU 只有 24 logical processors？

實測 24→32 threads 仍有約 8% 改善，可能來自更好的排程與 I/O 等待重疊；但平均 CPU occupancy 幾乎不再增加，表示已接近飽和。Thread count 可由 CLI 控制且上限為 32。

## Benchmark 與工程決策

### 17. 如何確保 benchmark 可信？

使用 Release build、固定 seed 與相同硬體，先 warm-up，再至少跑五次並取 median。每輪直接比對 raw stdout bytes；同時記錄 input bytes、吞吐量、CPU、RAM、compiler 與 cache mode。

### 18. 為什麼不用平均時間？

平均值容易被背景工作、排程或偶發 I/O spike 拉動；median 對離群值較穩健。小幅優化另外使用交錯 A/B runs，降低溫度與執行順序偏差。

### 19. 有哪些優化最後沒有保留？

Whole-file mapping 的速度與 working-set 取捨不佳。另一個固定分支 parser 在 1B 僅改善 1.65%，100M 單執行緒僅改善 0.33%，落在測量波動內，因此撤回。負面結果也被記錄，避免日後重複投入。

### 20. 1B 到 2B 時間會如何變化？

若仍為 CPU-bound，時間大致線性增加，而記憶體維持近似不變；主要版本可粗估約兩倍時間。但 2B 約 25.5 GiB，接近系統 RAM，filesystem cache 與 SSD I/O 可能改變瓶頸，因此必須實測，不能把 1B 百分比直接視為保證。

### 21. 專案最大的技術成果是什麼？

不只是把 1B 從 554.82 秒縮短到約 3.69 秒，而是在 byte-identical correctness、受控 allocation failure 和約 10.58 MiB committed memory 下達成約 150 倍加速，並保留每個階段的測量證據與回復點。

### 22. 如果再做下一步，會選什麼？

先取得可靠的函式級 sampling profile，或用隔離實驗分析 record scanning 與 hash 成本。只有證據顯示值得時才考慮 SIMD；不會為了追求排行榜而犧牲主要版本的可讀性、可維護性或記憶體穩定性。
