// minimal stub plugin system to allow build and tests without depending on plugin header
int tinybuf_plugin_register(const unsigned char *types, int type_count, void *read, void *write){
    (void)types; (void)type_count; (void)read; (void)write; return -1;
}
int tinybuf_plugin_unregister_all(void){ return 0; }
int tinybuf_plugins_try_read_by_type(unsigned char type, void *buf, void *out, void *contain_handler){
    (void)type; (void)buf; (void)out; (void)contain_handler; return -1;
}
int tinybuf_plugins_try_write(unsigned char type, const void *in, void *out){
    (void)type; (void)in; (void)out; return -1;
}
int tinybuf_try_read_box_with_plugins(void *buf, void *out, void *contain_handler){
    (void)buf; (void)out; (void)contain_handler; return -1;
}
