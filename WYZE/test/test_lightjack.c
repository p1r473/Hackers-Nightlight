/* Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only */
#include "lightjack/lightjack_levels.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned cases;
static lightjack_levels_t parse(const char *body)
{
    lightjack_levels_t result = {{999,999,999,999,999}};
    assert(lightjack_parse_levels(body, strlen(body), &result));
    ++cases;
    return result;
}
static void reject(const char *body)
{
    lightjack_levels_t result = {{999,999,999,999,999}};
    assert(!lightjack_parse_levels(body, strlen(body), &result));
    for (unsigned i=0; i<5; ++i) assert(result.channel[i] == 999);
    ++cases;
}
struct reader { const char *body; size_t offset, chunk, stop; int failure; };
static int read_chunk(void *context, char *buffer, size_t length)
{
    struct reader *reader = context;
    if (reader->offset >= reader->stop) return reader->failure;
    if (length > reader->chunk) length = reader->chunk;
    if (length > reader->stop-reader->offset) length = reader->stop-reader->offset;
    memcpy(buffer, reader->body + reader->offset, length);
    reader->offset += length;
    return (int)length;
}
struct writer { unsigned calls, fail_at; uint16_t value[20]; uint8_t channel[20]; };
static int write_channel(void *context, uint8_t channel, uint16_t value)
{
    struct writer *writer = context;
    assert(writer->calls < 20);
    writer->channel[writer->calls] = channel;
    writer->value[writer->calls++] = value;
    return writer->calls == writer->fail_at ? 37 : 0;
}

int main(void)
{
    lightjack_levels_t result = parse("{\"brightness\":\"1024\",\"warm\":\"0\",\"rgb\":{\"r\":255,\"g\":127,\"b\":0}}");
    assert(result.channel[0]==0 && result.channel[1]==63 && result.channel[2]==128 && !result.channel[3] && !result.channel[4]);
    result = parse("{\"brightness\":\"512\",\"warm\":\"1024\"}");
    assert(!result.channel[0] && !result.channel[1] && !result.channel[2] && result.channel[3]==128 && result.channel[4]==64);
    result = parse("{\"brightness\":\"0\",\"warm\":\"1\"}");
    for (unsigned i=0;i<5;++i) assert(result.channel[i]==0);
    result = parse(" {\"brightness\":\"0\",\"rgb\":{\"r\":255,\"g\":255,\"b\":255}}\r\n");
    for (unsigned i=0;i<5;++i) assert(result.channel[i]==0);
    const char *bad[] = {
        "", "{}", "null", "[]", "{", "{\"brightness\":\"1\"}",
        "{\"brightness\":\"1\\u0000junk\",\"warm\":\"1\"}",
        "{\"brightness\":1,\"warm\":\"1\"}", "{\"brightness\":\"-1\",\"warm\":\"1\"}",
        "{\"brightness\":\"1025\",\"warm\":\"1\"}", "{\"brightness\":\"9999999999999\",\"warm\":\"1\"}",
        "{\"brightness\":\"1junk\",\"warm\":\"1\"}", "{\"brightness\":\"1\",\"warm\":null}",
        "{\"brightness\":\"1\",\"warm\":\"1\",\"warm\":\"2\"}",
        "{\"brightness\":\"1\",\"warm\":\"1\",\"extra\":0}",
        "{\"brightness\":\"1\",\"warm\":\"1\"} trailing",
        "{\"brightness\":\"1\",\"warm\":\"1\"}{}",
        "{\"brightness\":\"1\",\"rgb\":{}}",
        "{\"brightness\":\"1\",\"rgb\":{\"r\":0,\"g\":0}}",
        "{\"brightness\":\"1\",\"rgb\":{\"r\":256,\"g\":0,\"b\":0}}",
        "{\"brightness\":\"1\",\"rgb\":{\"r\":-1,\"g\":0,\"b\":0}}",
        "{\"brightness\":\"1\",\"rgb\":{\"r\":0.1,\"g\":0,\"b\":0}}",
        "{\"brightness\":\"1\",\"rgb\":{\"r\":true,\"g\":0,\"b\":0}}",
        "{\"brightness\":\"1\",\"rgb\":{\"r\":\"1\",\"g\":0,\"b\":0}}",
        "{\"brightness\":\"1\",\"rgb\":{\"r\":1e999,\"g\":0,\"b\":0}}",
        "{\"brightness\":\"1\",\"rgb\":{\"r\":0,\"r\":1,\"g\":0,\"b\":0}}",
        "{\"brightness\":\"1\",\"warm\":\"1\",\"rgb\":null}"
    };
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) reject(bad[i]);
    const char embedded[] = "{\"brightness\":\"1\",\"warm\":\"1\"}\0garbage";
    assert(!lightjack_parse_levels(embedded, sizeof(embedded)-1, &result)); ++cases;
    char oversized[LIGHTJACK_BODY_MAX+2]; memset(oversized, ' ', sizeof(oversized));
    assert(!lightjack_parse_levels(oversized,sizeof(oversized),&result)); ++cases;

    /* Exhaustive brightness endpoints across every RGB input value. */
    for (unsigned component=0;component<=255;++component) {
        char json[160];
        snprintf(json,sizeof(json),"{\"brightness\":\"1024\",\"rgb\":{\"r\":%u,\"g\":%u,\"b\":%u}}",component,component,component);
        result=parse(json);
        for(unsigned i=0;i<3;++i) assert(result.channel[i]==component*128/255);
    }
    const char *payload="{\"brightness\":\"128\",\"warm\":\"256\"}";
    const size_t length=strlen(payload);
    for(size_t chunk=1;chunk<=length;++chunk) {
        struct reader reader={payload,0,chunk,length,0};
        char body[LIGHTJACK_BODY_MAX+1]; memset(body,0x7f,sizeof(body));
        assert(lightjack_receive(read_chunk,&reader,length,body)==0);
        assert(strcmp(body,payload)==0); ++cases;
    }
    for (size_t stop=0;stop<length;++stop) {
        struct reader reader={payload,0,2,stop,0};
        char body[LIGHTJACK_BODY_MAX+1];
        assert(lightjack_receive(read_chunk,&reader,length,body)==-2); ++cases;
        reader.offset=0; reader.failure=-3;
        assert(lightjack_receive(read_chunk,&reader,length,body)==-3); ++cases;
    }
    struct reader reader={payload,0,1,length,0};
    char body[LIGHTJACK_BODY_MAX+1];
    assert(lightjack_receive(read_chunk,&reader,0,body)==-1 && reader.offset==0);
    assert(lightjack_receive(read_chunk,&reader,LIGHTJACK_BODY_MAX+1,body)==-1 && reader.offset==0); cases+=2;

    lightjack_levels_t requested={{10,20,30,0,0}};
    struct writer writer={0};
    assert(lightjack_apply_levels(&requested,write_channel,&writer)==0);
    assert(writer.calls==8);
    for(unsigned i=0;i<5;++i) assert(writer.channel[i]==i && writer.value[i]==0);
    for(unsigned i=0;i<3;++i) assert(writer.channel[5+i]==i && writer.value[5+i]==requested.channel[i]); ++cases;
    for(unsigned failure=1;failure<=8;++failure) {
        memset(&writer,0,sizeof(writer)); writer.fail_at=failure;
        assert(lightjack_apply_levels(&requested,write_channel,&writer)==37);
        if(failure<=5) { assert(writer.calls==5); for(unsigned i=0;i<5;++i) assert(writer.value[i]==0); }
        else { assert(writer.calls==failure+5); for(unsigned i=0;i<5;++i) assert(writer.channel[failure+i]==i && writer.value[failure+i]==0); }
        ++cases;
    }
    requested.channel[4]=129; memset(&writer,0,sizeof(writer));
    assert(lightjack_apply_levels(&requested,write_channel,&writer)!=0 && writer.calls==0); ++cases;
    printf("PASS: %u parser, fragmented receive, mapping and I/O fault cases\n",cases);
    return 0;
}
