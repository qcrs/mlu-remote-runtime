"""Bounded trailing u64 arrays, encoded elementwise in network byte order.

Request arrays are borrowed for the synchronous call. Response arrays are
caller-owned and sized by a fixed request field; no pointer crosses the wire.
"""
def generate_array_rpc(r, header, opcodes, client, server, backend, valid, decls,
                       fields_decl, size, codec):
    name=r['name']; op='MLU_OP_'+r['operation']; req=r['request']; res=r['response']
    ra=r.get('request_array'); sa=r.get('response_array'); nr=size(req); ns=size(res)
    def offset(field): return size(req[:next(i for i,f in enumerate(req) if f['name']==field)])
    opcodes.append(f"{op} = {r['opcode']},")
    for direction,fs,a in [('request',req,ra),('response',res,sa)]:
        header += ['typedef struct {',*fields_decl(fs)]
        if a: header += [f"  {'const ' if direction=='request' else ''}uint64_t *{a['name']};"]
        header += [f'}} mlu_api_{name}_{direction};']
    header += [f'int mlu_remote_api_{name}(mlu_remote_context *, const mlu_api_{name}_request *, mlu_api_{name}_response *);']
    conditions=[f'request_len >= {nr}']
    if ra: conditions += [f'get64(request+{offset(ra["size_from"])}) <= {ra["max_count"]}u',f'request_len == {nr}+8*get64(request+{offset(ra["size_from"])})']
    else: conditions += [f'request_len == {nr}']
    if sa: conditions += [f'get64(request+{offset(sa["size_from"])}) <= {sa["max_count"]}u',f'n == {ns}+8*get64(request+{offset(sa["size_from"])})']
    else: conditions += [f'n == {ns}']
    valid += [f'case {op}: return '+' && '.join(conditions)+';']
    client += [f'int mlu_remote_api_{name}(mlu_remote_context *ctx, const mlu_api_{name}_request *request, mlu_api_{name}_response *response) {{',
        '  if(!ctx || !request || !response) return MLU_REMOTE_BAD_LENGTH;',
        '  if(!(ctx->capabilities & MLU_CAP_TYPED_API)) return MLU_REMOTE_UNSUPPORTED;',
        f'  mlu_api_{name}_request in=*request;', '  unsigned char *e=NULL; uint32_t n=0;']
    for a in (ra,sa):
        if a:
            ptr=f'in.{a["name"]}' if a is ra else f'response->{a["name"]}'
            client += [f'  if(in.{a["size_from"]}>{a["max_count"]}u || (in.{a["size_from"]} && !{ptr})) return MLU_REMOTE_BAD_LENGTH;']
    client += [f'  uint32_t length={nr}'+(f'+8*(uint32_t)in.{ra["size_from"]}' if ra else '')+';',
        '  unsigned char *p=malloc(length); if(!p) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;',*codec(req,'in','p',True)]
    if ra: client += [f'  for(uint64_t i=0;i<in.{ra["size_from"]};i++) put64(p+{nr}+8*i,in.{ra["name"]}[i]);']
    client += [f'  int st=rpc(ctx,{op},p,length,&e,&n); free(p);',f'  if(!st) {{ mlu_api_{name}_response out={{0}};',*codec(res,'out','e',False)]
    if sa: client += [f'    out.{sa["name"]}=response->{sa["name"]};', f'    for(uint64_t i=0;i<in.{sa["size_from"]};i++) out.{sa["name"]}[i]=get64(e+{ns}+8*i);']
    client += ['    *response=out;', '  } free(e); return st;', '}']
    server += [f'case {op}: {{',f'  if(n<{nr}) return send_response(fd,op,req,MLU_REMOTE_BAD_LENGTH,NULL,0);',
        f'  mlu_api_{name}_request in={{0}}; mlu_api_{name}_response result={{0}};',*codec(req,'in','p',False)]
    if ra: server += [f'  if(in.{ra["size_from"]}>{ra["max_count"]}u || n!={nr}+8*in.{ra["size_from"]}) return send_response(fd,op,req,MLU_REMOTE_BAD_LENGTH,NULL,0);']
    else: server += [f'  if(n!={nr}) return send_response(fd,op,req,MLU_REMOTE_BAD_LENGTH,NULL,0);']
    if sa: server += [f'  if(in.{sa["size_from"]}>{sa["max_count"]}u) return send_response(fd,op,req,MLU_REMOTE_BAD_LENGTH,NULL,0);']
    if ra:
        server += [f'  uint64_t *input_ids=calloc(in.{ra["size_from"]} ? in.{ra["size_from"]} : 1,8);',
            '  if(!input_ids) return send_response(fd,op,req,CN_ERROR_SYSTEM_OUT_OF_MEMORY,NULL,0);',
            f'  for(uint64_t i=0;i<in.{ra["size_from"]};i++) input_ids[i]=get64(p+{nr}+8*i);',f'  in.{ra["name"]}=input_ids;']
    server += [f'  uint32_t encoded_size={ns}'+(f'+8*(uint32_t)in.{sa["size_from"]}' if sa else '')+';',
        '  unsigned char *encoded=calloc(encoded_size ? encoded_size : 1,1);']
    if sa: server += [f'  uint64_t *output_ids=calloc(in.{sa["size_from"]} ? in.{sa["size_from"]} : 1,8);']
    server += ['  if(!encoded'+(' || !output_ids' if sa else '')+') { free(encoded);'+(' free(input_ids);' if ra else '')+(' free(output_ids);' if sa else '')+' return send_response(fd,op,req,CN_ERROR_SYSTEM_OUT_OF_MEMORY,NULL,0); }']
    if sa: server += [f'  result.{sa["name"]}=output_ids;']
    server += [f'  st=mlu_backend_api_{name}(s,&in,&result);',*codec(res,'result','encoded',True)]
    if sa: server += [f'  if(!st) for(uint64_t i=0;i<in.{sa["size_from"]};i++) put64(encoded+{ns}+8*i,output_ids[i]);']
    server += ['  int sent=send_response(fd,op,req,(uint32_t)st,st ? NULL : encoded,st ? 0 : encoded_size);',
        '  free(encoded);'+(' free(input_ids);' if ra else '')+(' free(output_ids);' if sa else '')+' return sent;', '}']
    decl=f'int mlu_backend_api_{name}(mlu_backend_session *s, const mlu_api_{name}_request *in, mlu_api_{name}_response *out)'
    decls += [decl+';']; backend += [decl+' {',*['  '+x for x in r['backend_binding']],'}']
