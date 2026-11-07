"""Bounded borrowed byte requests and caller-owned byte responses."""
def generate_buffer_rpc(r, header, opcodes, client, server, backend, valid, decls,
                        fields_decl, size, codec):
    name=r['name']; op='MLU_OP_'+r['operation']; req=r['request']; res=r['response']
    rb=r['request_buffer']; sb=r.get('response_buffer'); nr=size(req); ns=size(res)
    def offset(field): return size(req[:next(i for i,f in enumerate(req) if f['name']==field)])
    opcodes.append(f"{op} = {r['opcode']},")
    for direction,fs,b in [('request',req,rb),('response',res,sb)]:
        header += ['typedef struct {',*fields_decl(fs)]
        if b: header += [f"  {'const ' if direction=='request' else ''}unsigned char *{b['name']};"]
        header += [f'}} mlu_api_{name}_{direction};']
    header += [f'int mlu_remote_api_{name}(mlu_remote_context *, const mlu_api_{name}_request *, mlu_api_{name}_response *);']
    conditions=[f'request_len >= {nr}',f'get64(request+{offset(rb["size_from"])}) <= {rb["max_bytes"]}u',f'request_len == {nr}+get64(request+{offset(rb["size_from"])})']
    if sb: conditions += [f'get64(request+{offset(sb["size_from"])}) <= {sb["max_bytes"]}u',f'n == {ns}+get64(request+{offset(sb["size_from"])})']
    else: conditions += [f'n == {ns}']
    valid += [f'case {op}: return '+' && '.join(conditions)+';']
    client += [f'int mlu_remote_api_{name}(mlu_remote_context *ctx, const mlu_api_{name}_request *request, mlu_api_{name}_response *response) {{',
        '  if(!ctx || !request || !response) return MLU_REMOTE_BAD_LENGTH;',
        '  if(!(ctx->capabilities & MLU_CAP_TYPED_API)) return MLU_REMOTE_UNSUPPORTED;',
        f'  mlu_api_{name}_request in=*request;', '  unsigned char *e=NULL; uint32_t n=0;']
    for b,ptr in [(rb,f'in.{rb["name"]}')]+([(sb,f'response->{sb["name"]}')] if sb else []):
        client += [f'  if(in.{b["size_from"]}>{b["max_bytes"]}u || (in.{b["size_from"]} && !{ptr})) return MLU_REMOTE_BAD_LENGTH;']
    client += [f'  uint32_t length={nr}+(uint32_t)in.{rb["size_from"]};',
        '  unsigned char *p=malloc(length); if(!p) return CN_ERROR_SYSTEM_OUT_OF_MEMORY;',*codec(req,'in','p',True),
        f'  if(in.{rb["size_from"]}) memcpy(p+{nr},in.{rb["name"]},in.{rb["size_from"]});',
        f'  int st=rpc(ctx,{op},p,length,&e,&n); free(p);',f'  if(!st) {{ mlu_api_{name}_response out={{0}};',*codec(res,'out','e',False)]
    if sb: client += [f'    out.{sb["name"]}=response->{sb["name"]};', f'    if(in.{sb["size_from"]}) memcpy(out.{sb["name"]},e+{ns},in.{sb["size_from"]});']
    client += ['    *response=out;', '  } free(e); return st;', '}']
    server += [f'case {op}: {{',f'  if(n<{nr}) return send_response(fd,op,req,MLU_REMOTE_BAD_LENGTH,NULL,0);',
        f'  mlu_api_{name}_request in={{0}}; mlu_api_{name}_response result={{0}};',*codec(req,'in','p',False),
        f'  if(in.{rb["size_from"]}>{rb["max_bytes"]}u || n!={nr}+in.{rb["size_from"]}) return send_response(fd,op,req,MLU_REMOTE_BAD_LENGTH,NULL,0);',
        f'  in.{rb["name"]}=p+{nr};']
    if sb: server += [f'  if(in.{sb["size_from"]}>{sb["max_bytes"]}u) return send_response(fd,op,req,MLU_REMOTE_BAD_LENGTH,NULL,0);']
    server += [f'  uint32_t encoded_size={ns}'+(f'+(uint32_t)in.{sb["size_from"]}' if sb else '')+';',
        '  unsigned char *encoded=calloc(encoded_size ? encoded_size : 1,1);',
        '  if(!encoded) return send_response(fd,op,req,CN_ERROR_SYSTEM_OUT_OF_MEMORY,NULL,0);']
    if sb: server += [f'  result.{sb["name"]}=encoded+{ns};']
    server += [f'  st=mlu_backend_api_{name}(s,&in,&result);',*codec(res,'result','encoded',True),
        '  int sent=send_response(fd,op,req,(uint32_t)st,st ? NULL : encoded,st ? 0 : encoded_size);',
        '  free(encoded); return sent;', '}']
    decl=f'int mlu_backend_api_{name}(mlu_backend_session *s, const mlu_api_{name}_request *in, mlu_api_{name}_response *out)'
    decls += [decl+';']; backend += [decl+' {',*['  '+x for x in r['backend_binding']],'}']
