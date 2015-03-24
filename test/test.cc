#include <iostream>
#include <string>
#include <map>

using namespace std;

typedef map<string, string> STR_MAP;
static int ngx_url_parser(const string &url, STR_MAP &kv);

int main() {
    string url;

    while(getline(cin, url)) {
        STR_MAP kv;

        int rc = ngx_url_parser(url, kv);
        if(rc < 0) {
            cout << "Invalid url: " << url << endl;
            continue;
        }       

        STR_MAP::iterator it = kv.begin();
        cout << "All key value: " << endl;
        for(; it != kv.end(); it++) {
            cout << it->first << "=" << it->second << endl;
        }
    }

    return 0;
}

static int ngx_url_parser(const string &url, STR_MAP &kv) {
    const char *url_cstr = url.c_str();
    char *beg = (char *)url_cstr, *end = beg + url.length();
    
    while(beg < end) {
        char *delimiter = beg, *equal = beg;

        while(delimiter < end && *delimiter != '&') delimiter++;
        while(equal < delimiter && *equal != '=') equal++;
        
        if(equal == beg) {          /* key can't be empty */
            beg = delimiter + 1;
            continue;
        }
        string key = string(beg, equal - beg);

        string val;
        if((delimiter - equal - 1) > 0) {
            val = string(equal + 1, delimiter - equal - 1);
        } else {
            val = string("");
        }

        kv.insert(make_pair(key, val));

        beg = delimiter + 1;
    }

    return 0;
}
