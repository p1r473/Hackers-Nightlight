/* Lightjack adapter failure-path tests, GPL-3.0-only, 2026-10-21. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
/* Compile the real adapter in this translation unit to reset its private state
 * between simulated boots. No board libraries or radio actions are linked. */
#include "../src/lightjack/lightjack_adapter.c"

static esp_partition_t partitions[] = {
    {"nvs",0x9000,0x4000},{"otadata",0xd000,0x2000},{"phy_init",0xf000,0x1000},
    {"ota_0",0x10000,0x1e0000},{"ota_1",0x1f0000,0x1e0000},
    {"mac",0x3d0000,0x4000},{"iot",0x3d4000,0x4000},{"user",0x3d8000,0x8000},
    {"factory_nvs",0x3e0000,0x6000},{"reserved",0x3e6000,0x12000}
};
static int nvs_result, boot_result, description_result, init_result;
static unsigned selected, nvs_calls, arduino_calls, driver_calls, writes, fail_write;
static bool bad_project, no_other;
static jmp_buf suspended;

static void reset(void)
{
    ready=early_ready=false;
    nvs_result=boot_result=description_result=init_result=0;
    selected=nvs_calls=arduino_calls=driver_calls=writes=fail_write=0;
    bad_project=no_other=false;
}
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label)
{
    (void)type; (void)subtype;
    for(unsigned i=0;i<10;++i) if(!strcmp(label,partitions[i].label)) return &partitions[i];
    return NULL;
}
const esp_partition_t *esp_ota_get_running_partition(void) { return &partitions[3]; }
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p)
{ (void)p; return no_other ? NULL : &partitions[4]; }
esp_err_t esp_ota_get_partition_description(const esp_partition_t *p,esp_app_desc_t *desc)
{
    assert(p==&partitions[4]);
    memset(desc,0,sizeof(*desc));
    strcpy(desc->project_name,bad_project ? "unexpected" : "wyze_loader");
    return description_result;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p)
{ assert(p==&partitions[4]); assert(nvs_calls==0); ++selected; return boot_result; }
esp_err_t nvs_flash_init(void) { assert(selected==1); ++nvs_calls; return nvs_result; }
void __real_initArduino(void) { assert(early_ready); assert(nvs_calls==1); ++arduino_calls; }
void vTaskSuspend(void *task) { assert(!task); longjmp(suspended,1); }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test error"; }
esp_err_t bulb_write_init(void) { assert(arduino_calls==1); ++driver_calls; return init_result; }
esp_err_t bulb_write(uint8_t channel,uint16_t value)
{
    assert(channel<5 && value<=128);
    ++writes;
    return writes==fail_write ? 42 : 0;
}
uint8_t bulb_channel_count(void) { return 5; }
const char *bulb_channel_name(uint8_t channel) { (void)channel; return "test"; }
int httpd_req_recv(httpd_req_t *req,char *buffer,size_t length)
{
    ++req->recv_calls;
    size_t remaining=strlen(req->body)-req->offset;
    if(!remaining) return HTTPD_SOCK_ERR_TIMEOUT;
    if(length>remaining) length=remaining;
    if(length>req->chunk) length=req->chunk;
    memcpy(buffer,req->body+req->offset,length); req->offset+=length;
    return (int)length;
}
esp_err_t httpd_resp_set_status(httpd_req_t *req,const char *text)
{ assert(strlen(text)<sizeof(req->status)); strcpy(req->status,text); return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t *req,const char *text)
{ (void)req; (void)text; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *req,const char *name,const char *value)
{ (void)req; (void)name; (void)value; return ESP_OK; }
esp_err_t httpd_resp_sendstr(httpd_req_t *req,const char *text)
{ assert(strlen(text)<sizeof(req->response)); strcpy(req->response,text); return ESP_OK; }
esp_err_t httpd_resp_send(httpd_req_t *req,const char *text,int size)
{ (void)req; (void)text; (void)size; return ESP_OK; }

static void expect_stop(void)
{
    if(setjmp(suspended)==0) { __wrap_initArduino(); assert(!"expected suspended startup"); }
    assert(!early_ready && !arduino_calls && !driver_calls && !writes);
}
static httpd_req_t request(const char *body)
{
    httpd_req_t req={0}; req.body=body; req.content_len=strlen(body); req.chunk=3;
    return req;
}
int main(void)
{
    reset(); assert(lightjack_prepare()==ESP_ERR_INVALID_STATE && !driver_calls);
    reset(); partitions[9].size++; expect_stop(); partitions[9].size--;
    assert(!selected && !nvs_calls);
    reset(); bad_project=true; expect_stop(); assert(!selected && !nvs_calls);
    reset(); no_other=true; expect_stop(); assert(!selected && !nvs_calls);
    reset(); description_result=43; expect_stop(); assert(!selected && !nvs_calls);
    reset(); boot_result=44; expect_stop(); assert(selected==1 && !nvs_calls);
    reset(); nvs_result=ESP_ERR_NVS_NO_FREE_PAGES; expect_stop(); assert(selected==1 && nvs_calls==1);
    reset(); nvs_result=45; expect_stop(); assert(selected==1 && nvs_calls==1);
    reset(); __wrap_initArduino(); assert(early_ready && arduino_calls==1);
    init_result=46; assert(lightjack_prepare()==46 && !ready && writes==5);
    reset(); __wrap_initArduino(); fail_write=2;
    assert(lightjack_prepare()==42 && !ready && writes==5);
    reset(); __wrap_initArduino(); assert(lightjack_prepare()==ESP_OK && ready && writes==5);
    const char *good="{\"brightness\":\"1024\",\"rgb\":{\"r\":255,\"g\":255,\"b\":255}}";
    httpd_req_t req=request(good); writes=0;
    assert(lightjack_color_handler(&req)==ESP_OK && !strcmp(req.status,"200 OK") && writes==8 && req.recv_calls>1);
    req=request("{\"brightness\":\"1\"}"); writes=0;
    assert(lightjack_color_handler(&req)==ESP_OK && !strcmp(req.status,"400 Bad Request") && !writes);
    req=request(good); req.content_len=385;
    assert(lightjack_color_handler(&req)==ESP_FAIL && !strcmp(req.status,"400 Bad Request") && !req.recv_calls && !writes);
    req=request(good); ++req.content_len;
    assert(lightjack_color_handler(&req)==ESP_FAIL && !strcmp(req.status,"408 Request Timeout") && !writes);
    req=request(good); fail_write=6;
    assert(lightjack_color_handler(&req)==ESP_OK && !strcmp(req.status,"503 Service Unavailable") && !ready && writes==11);
    req=request(good); fail_write=0; writes=0;
    assert(lightjack_color_handler(&req)==ESP_OK && !strcmp(req.status,"503 Service Unavailable") && writes==5);
    req=request("private credentials"); writes=0;
    assert(lightjack_update_disabled(&req)==ESP_FAIL && !strcmp(req.status,"409 Conflict") && !req.recv_calls && !writes);
    puts("PASS: 18 boot, NVS, HTTP, driver-fault and updater scenarios");
    return 0;
}
