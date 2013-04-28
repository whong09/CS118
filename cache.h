#ifndef _CACHE_H_
#define _CACHE_H_

#include <string>
#include "http-request.h"

#define PATH "./cache/"

//check the cache given a request
bool checkCache(HttpRequest req);

//get the cache page given a request
//perhaps use conditional get in this function?
string getCache(HttpRequest req);

//store a cache page given a request and a page
void storeCache(HttpRequest req, string cache);