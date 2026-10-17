// Compiled after the real init, draw and restart functions by test_crowpanel_rgb.py.
static esp_rgb_panel_t *observed;
static void restartDuringWriteback() {
    // A VSYNC can interrupt cache writeback. It must keep the previous frame.
    assert(observed->cur_fb_index == 0);
    lcd_rgb_panel_try_restart_transmission(observed);
    assert(started == observed->dma_restart_links[0]);
}
int main() {
    std::vector<uint8_t> storage[3];
    for (auto &fb : storage) fb.resize(480 * 480 * 2);
    for (size_t alignment : {size_t(4), size_t(32), size_t(64)}) {
    for (int count = 1; count <= 3; ++count) {
        esp_rgb_panel_t panel;
        panel.num_fbs = count; panel.fb_size = storage[0].size(); panel.ext_mem_align = alignment;
        for (int i = 0; i < count; ++i) panel.fbs[i] = storage[i].data();
        assert(lcd_rgb_panel_init_trans_link(&panel) == ESP_OK);
        for (int i = 0; i < count; ++i) {
            auto *restart = panel.dma_restart_links[i];
            assert(restart && restart->buffer == panel.fbs[i] + 34);
            assert(restart->length + 34 == panel.dma_fb_links[i]->length);
            assert(restart->next == panel.dma_fb_links[i] && restart->next_item == 1);
        }
        observed = &panel;
        if (count >= 2) {
            syncedBytes = syncCalls = 0;
            duringCacheSync = restartDuringWriteback;
            assert(rgb_panel_draw_bitmap(&panel.base, 0, 0, 480, 480, panel.fbs[1]) == ESP_OK);
            duringCacheSync = nullptr;
            assert(panel.cur_fb_index == 1);
            assert(syncedBytes == panel.fb_size && syncCalls == (panel.fb_size + 4095) / 4096);
            syncResult = -1;
            assert(rgb_panel_draw_bitmap(&panel.base, 0, 0, 480, 480, panel.fbs[0]) != ESP_OK);
            assert(panel.cur_fb_index == 1); // Failed writeback cannot publish a partial frame.
            syncResult = ESP_OK;
        }
        for (int repeat = 0; repeat < 8; ++repeat) {
            const int index = repeat % count;
            assert(rgb_panel_draw_bitmap(&panel.base, 0, 0, 480, 480, panel.fbs[index]) == ESP_OK);
#if CROWPANEL_HAS_FRAME_STATUS
            const auto submitted = panel.frame_submitted;
            assert(submitted != panel.frame_presented);
#endif
            lcd_rgb_panel_try_restart_transmission(&panel);
#if CROWPANEL_HAS_FRAME_STATUS
            assert(panel.frame_presented == submitted);
#endif
            assert(started == panel.dma_restart_links[index]);
            assert(started->buffer == panel.fbs[index] + 34);
            for (int j = 0; j < count; ++j)
                assert(panel.dma_fb_links[j]->next == panel.dma_fb_links[index]);
        }
        assert(panel.restart_stats.frame_buffer_mask == (1u << count) - 1);
        const auto previous = panel.restart_stats.count;
        for (int idle = 0; idle < 50; ++idle) lcd_rgb_panel_try_restart_transmission(&panel);
        assert(panel.restart_stats.count == previous + 50); // Recover even without UI redraws.
        assert(rgb_panel_draw_bitmap(&panel.base, 0, 0, 480, 480, panel.fbs[0]) == ESP_OK);
#if CROWPANEL_HAS_FRAME_STATUS
        const auto beforeFailure = panel.frame_presented;
#endif
        startResult = -1;
        lcd_rgb_panel_try_restart_transmission(&panel);
        assert(panel.restart_stats.count == previous + 50 && panel.restart_stats.errors == 1);
#if CROWPANEL_HAS_FRAME_STATUS
        assert(panel.frame_presented == beforeFailure);
#endif
        startResult = ESP_OK;
    }
    }
    // Healthy bounce frames run continuously; a missed refill triggers recovery.
    esp_rgb_panel_t bounce;
    bounce.num_fbs = 1; bounce.fb_size = storage[0].size(); bounce.bb_size = 480 * 20 * 2;
    bounce.expect_eof_count = 24;
    bounce.fbs[0] = storage[0].data();
    bounce.bounce_buffer[0] = storage[1].data(); bounce.bounce_buffer[1] = storage[2].data();
    assert(lcd_rgb_panel_init_trans_link(&bounce) == ESP_OK);
    started = nullptr;
    bounce.bb_eof_count = 24;
    lcd_rgb_panel_try_restart_transmission(&bounce);
    assert(started == nullptr && bounce.restart_stats.count == 0 && bounce.bb_eof_count == 0);
    bounce.bb_eof_count = 23;
    lcd_rgb_panel_try_restart_transmission(&bounce);
    assert(bounce.restart_stats.count == 1 && bounce.bounce_pos_px == 480 * 40);
    assert(started == bounce.dma_restart_links[0]);
    assert(started->buffer == bounce.bounce_buffer[0] + 34 && started->next == bounce.dma_bb_link);
    assert(started->length + 34 == bounce.dma_bb_link->length);
    // A frame published during scanout is latched only after the last old chunk
    // has been copied. From then on all refills use the newly submitted frame.
    std::vector<uint8_t> nextFrame(bounce.fb_size, 0x5a);
    std::fill(storage[0].begin(), storage[0].end(), 0xa5);
    bounce.num_fbs = 2; bounce.fbs[1] = nextFrame.data();
    bounce.cur_fb_index = 0; bounce.bb_fb_index = 0;
    bounce.frame_submitted = bounce.frame_presented = 3;
    const auto previousSyncCalls = syncCalls;
    assert(rgb_panel_draw_bitmap(&bounce.base, 0, 0, 480, 480, bounce.fbs[1]) == ESP_OK);
    assert(syncCalls == previousSyncCalls); // Bounce DMA reads SRAM, so no PSRAM writeback.
    assert(bounce.bb_fb_index == 0 && bounce.cur_fb_index == 1);
#if CROWPANEL_HAS_FRAME_STATUS
    assert(bounce.frame_submitted == 4 && bounce.frame_presented == 3);
#endif
    bounce.bounce_pos_px = (bounce.fb_size - bounce.bb_size) / 2;
    bounce.on_frame_buf_complete = [](esp_lcd_panel_t*, void*, void*) {
        assert(lockDepth == 1); // VSYNC cannot interrupt the EOF refill/handoff.
        return false;
    };
    lcd_rgb_panel_eof_handler(0, nullptr, &bounce);
    assert(bounce.bb_eof_count == 1 && lockDepth == 0);
    assert(std::all_of(storage[1].begin(), storage[1].begin() + bounce.bb_size,
                       [](uint8_t pixel) { return pixel == 0xa5; }));
    assert(bounce.bounce_pos_px == 0 && bounce.bb_fb_index == 1);
#if CROWPANEL_HAS_FRAME_STATUS
    assert(bounce.frame_presented == 4);
#endif
    lcd_rgb_panel_eof_handler(0, nullptr, &bounce);
    assert(bounce.bb_eof_count == 2 && lockDepth == 0);
    assert(std::all_of(storage[2].begin(), storage[2].begin() + bounce.bb_size,
                       [](uint8_t pixel) { return pixel == 0x5a; }));
    esp_rgb_panel_t failed;
    failed.num_fbs = 2; failed.fb_size = storage[0].size();
    failed.fbs[0] = storage[0].data(); failed.fbs[1] = storage[1].data();
    failAllocation = 3; // First two frame links and first restart link succeed.
    assert(lcd_rgb_panel_init_trans_link(&failed) == ESP_ERR_NO_MEM);
    assert(lockDepth == 0);
    for (auto *link : allocated) delete link;
    std::puts("PASS: actual RGB init/draw/restart/refill: DMA links, cache publication, recovery, errors and bounce frame handoff");
}
