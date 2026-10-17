// Host fakes for the actual extracted RGB driver functions. No DMA/panel emulation.
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using esp_err_t = int;
using gdma_channel_handle_t = int;
struct gdma_event_data_t {};
constexpr int ESP_OK = 0, ESP_ERR_NOT_SUPPORTED = -1, ESP_ERR_NO_MEM = -2;
#define IRAM_ATTR
#define CONFIG_IDF_TARGET_ESP32S3 1
#define unlikely(x) (x)
#define RGB_LCD_NEEDS_SEPARATE_RESTART_LINK 1
#define RGB_LCD_USE_GDMA_LINK_SWITCH_EVENT 0
#define RGB_LCD_PANEL_MAX_FB_NUM 3
#define RGB_LCD_PANEL_BOUNCE_BUF_NUM 2
#define LCD_GDMA_DESCRIPTOR_ALIGN 4
#define LCD_DMA_DESCRIPTOR_BUFFER_MAX_SIZE 4092
#define LCD_FIFO_PRESERVE_SIZE_PX 17
#define GDMA_FINAL_LINK_TO_DEFAULT 0
#define GDMA_FINAL_LINK_TO_NULL 1
#define ROTATE_MASK_SWAP_XY 1
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 1
#define ESP_CACHE_MSYNC_FLAG_UNALIGNED 2
#define MIN(a,b) std::min<size_t>((a),(b))
#define MAX(a,b) std::max((a),(b))
#define __containerof(ptr,type,member) reinterpret_cast<type*>(ptr)
#define ESP_LOGV(...) ((void)0)
#define ESP_RETURN_ON_FALSE(test,code,...) do { if (!(test)) return code; } while (0)
#define ESP_RETURN_ON_ERROR(call,...) do { int e = (call); if (e != ESP_OK) return e; } while (0)
#define COPY_PIXEL_CODE_BLOCK(bits) assert(false); // Tests submit complete owned framebuffers.
static unsigned lockDepth;
#define portENTER_CRITICAL(lock) (++lockDepth)
#define portEXIT_CRITICAL(lock) (--lockDepth)
#define portENTER_CRITICAL_ISR(lock) (++lockDepth)
#define portEXIT_CRITICAL_ISR(lock) (--lockDepth)
#define portENTER_CRITICAL_SAFE(lock) (++lockDepth)
#define portEXIT_CRITICAL_SAFE(lock) (--lockDepth)
struct esp_lcd_panel_t {};
using esp_lcd_rgb_panel_draw_buf_complete_cb_t = bool (*)(esp_lcd_panel_t*, void*, void*);
using esp_lcd_rgb_panel_frame_buf_complete_cb_t = bool (*)(esp_lcd_panel_t*, void*, void*);
struct Link { uint8_t *buffer{}; size_t length{}, items{}; Link *next{}; int next_item{}; };
using gdma_link_list_handle_t = Link*;
struct gdma_link_list_config_t { int item_alignment; size_t num_items; struct { bool check_owner; } flags; };
struct gdma_buffer_mount_config_t {
    void *buffer{}; size_t buffer_alignment{}, length{};
    struct { int mark_final{}; bool mark_eof{}, bypass_buffer_align_check{}; } flags;
};
struct crowpanel_rgb_restart_stats_t { uint32_t count{}, frame_buffer_mask{}, errors{}; };
struct esp_rgb_panel_t {
    esp_lcd_panel_t base;
    int num_fbs{};
    uint8_t *fbs[3]{}, *bounce_buffer[2]{};
    Link *dma_fb_links[3]{}, *dma_restart_links[3]{}, *dma_bb_link{}, *dma_restart_link{};
    size_t fb_size{}, bb_size{}, fb_bits_per_pixel{16}, int_mem_align{4}, ext_mem_align{64};
    uint8_t cur_fb_index{}, bb_fb_index{};
    int bounce_pos_px{}, bb_eof_count{}, expect_eof_count{}, spinlock{}, dma_chan{}, rotate_mask{}, x_gap{}, y_gap{};
    struct { int dev; } hal{};
    struct { int h_res{480}, v_res{480}; } timings;
    struct { bool fb_in_psram{true}, fb_behind_cache{true}, bb_behind_cache{}, stream_mode{true}, need_restart{}; } flags;
    esp_lcd_rgb_panel_draw_buf_complete_cb_t on_color_trans_done{};
    esp_lcd_rgb_panel_frame_buf_complete_cb_t on_frame_buf_complete{};
    bool (*on_bounce_empty)(esp_lcd_panel_t*, uint8_t*, int, size_t, void*){};
    void *user_ctx{};
    crowpanel_rgb_restart_stats_t restart_stats;
    uint32_t frame_submitted{}, frame_presented{};
};
static std::vector<Link*> allocated;
static Link *started;
static esp_err_t startResult = ESP_OK;
static int failAllocation = -1;
static void (*duringCacheSync)() = nullptr;
static size_t esp_dma_calculate_node_count(size_t bytes, size_t alignment, size_t maximum) {
    size_t capacity = maximum / alignment * alignment;
    return (bytes + capacity - 1) / capacity;
}
static esp_err_t gdma_new_link_list(const gdma_link_list_config_t *cfg, Link **out) {
    if (failAllocation == 0) return ESP_ERR_NO_MEM;
    if (failAllocation > 0) --failAllocation;
    *out = new Link; (*out)->items = cfg->num_items; allocated.push_back(*out); return ESP_OK;
}
static esp_err_t gdma_link_mount_buffers(Link *link, int, const gdma_buffer_mount_config_t *mount, int, void*) {
    const size_t capacity = 4095 / mount->buffer_alignment * mount->buffer_alignment;
    if (mount->length > capacity * link->items) return -1;
    link->buffer = static_cast<uint8_t*>(mount->buffer);
    link->length = std::min(mount->length, capacity); return ESP_OK;
}
static size_t gdma_link_get_length(Link *link, int) { return link->length; }
static esp_err_t gdma_link_concat(Link *a, int, Link *b, int index) {
    a->next = b; a->next_item = index; return ESP_OK;
}
static uintptr_t gdma_link_get_head_addr(Link *link) { return reinterpret_cast<uintptr_t>(link); }
static esp_err_t gdma_start(int, uintptr_t address) { started = reinterpret_cast<Link*>(address); return startResult; }
static void gdma_reset(int) {}
static void lcd_ll_fifo_reset(int) {}
static void Cache_Start_DCache_Preload(uintptr_t, size_t, int) {}
static size_t syncedBytes, syncCalls;
static esp_err_t syncResult = ESP_OK;
static esp_err_t esp_cache_msync(void*, size_t bytes, int) {
    assert(bytes <= 4096);
    syncedBytes += bytes;
    ++syncCalls;
    assert(lockDepth == 0); // Cache writeback must not block the VSYNC critical section.
    if (duringCacheSync) duringCacheSync();
    return syncResult;
}
