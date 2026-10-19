// SPDX-License-Identifier: MIT
#include <cassert>
#include "dom_handle_preview.h"
int main(){
 using namespace suitecut;
 DOMHandlePreviewNode outer;outer.type=1;outer.local_name=u"div";outer.attributes={{u"id",u"outer"},{u"name",u"value"}};outer.children={{1,u""}};
 assert(DOMHandlePreview(outer)==u"<div id=\"outer\" name=\"value\">…</div>");
 DOMHandlePreviewNode inner;inner.type=1;inner.local_name=u"div";inner.attributes={{u"id",u"inner"}};inner.children={{3,u"Text,\nmore text"}};
 assert(DOMHandlePreview(inner)==u"<div id=\"inner\">Text,↵more text</div>");
 DOMHandlePreviewNode text;text.type=3;text.value=u"Text,\nmore text";assert(DOMHandlePreview(text)==u"#text=Text,↵more text");
 DOMHandlePreviewNode input;input.type=1;input.local_name=u"input";input.attributes={{u"id",u"check"},{u"type",u"checkbox"},{u"checked",u""},{u"foo",u"bar\""}};
 assert(DOMHandlePreview(input)==u"<input checked id=\"check\" foo=\"bar\"\" type=\"checkbox\"/>");
 std::u16string emoji;for(int i=0;i<100;++i)emoji+=u"😛";const auto shortened=DOMHandlePreviewText(emoji);assert(shortened.size()==99&&shortened.back()==u'…');assert(shortened[97]>=0xdc00&&shortened[97]<=0xdfff);
 assert(DOMHandlePreviewText(std::u16string(100,u'a'))==std::u16string(100,u'a'));assert(DOMHandlePreviewText(std::u16string(101,u'a'))==std::u16string(99,u'a')+u"…");
 assert(DOMHandlePreviewText(u"a\r\nb\rc\nd")==u"a↵b↵c↵d");
 std::u16string isolated={u'a',char16_t(0xd800),u'b'};assert(DOMHandlePreviewText(isolated)==isolated);
 DOMHandlePreviewNode body;body.type=1;body.local_name=u"body";assert(DOMHandlePreview(body)==u"<body></body>");body.children_omitted=true;assert(DOMHandlePreview(body)==u"<body>…</body>");
 DOMHandlePreviewNode comment;comment.type=8;comment.value=u"actual";assert(DOMHandlePreview(comment)==u"<!--actual-->");
 DOMHandlePreviewNode root;root.type=11;root.name=u"#document-fragment";assert(DOMHandlePreview(root)==u"#document-fragment");
}
