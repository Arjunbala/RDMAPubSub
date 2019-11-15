// For now, assume that a client knows the IP of server.
// TODO: Replace this with a discovery service that identifies
// server based on the supplied topic name
void init(char *server);

// Add a record with a key and value
void produceRecord(char *key, char *value);

// Should be called only after init() at the end
void terminate();
