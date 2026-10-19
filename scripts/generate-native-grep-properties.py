# SPDX-License-Identifier: MIT
"""Generate exact ECMAScript Unicode17 property/alias/string data from public UCD.

This is an authored data projection, not a regex implementation import.
Generated data retain Unicode LicenseV3 and source hashes. Binary-property
allowlists come from the public ECMA262 normative property tables.
"""
import argparse
import hashlib
import json
from collections import defaultdict
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--data-directory',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a = p.parse_args();d = a.data_directory

def norm(ranges):
    result=[]
    for first,last in sorted(ranges):
        if result and first<=result[-1][1]+1:result[-1]=(result[-1][0],max(last,result[-1][1]))
        else:result.append((first,last))
    return result

def minus(left,right):
    result=[];j=0
    for first,last in left:
        cursor=first
        while j<len(right) and right[j][1]<cursor:j+=1
        k=j
        while k<len(right) and right[k][0]<=last:
            start,end=right[k]
            if cursor<start:result.append((cursor,start-1))
            cursor=max(cursor,end+1)
            if cursor>last:break
            k+=1
        if cursor<=last:result.append((cursor,last))
    return result

def span(text):
    values=text.strip().split('..');return int(values[0],16),int(values[-1],16)

def records(path):
    for line in path.read_text().splitlines():
        fields=[x.strip() for x in line.split('#',1)[0].strip().split(';')]
        if len(fields)>1:yield fields

tables=json.loads((d/'ecma262-property-tables.json').read_text());allowed={};canonical=None
for row in tables['table-binary-unicode-properties'][1:]:
    if len(row)>1:canonical=row[1]
    allowed[row[0]]=canonical
string_names=[row[0] for row in tables['table-binary-unicode-properties-of-strings'][1:]]
wanted=set(allowed.values());binary=defaultdict(list);category=defaultdict(list);first=None
for line in(d/'UnicodeData-17.0.0.txt').read_text().splitlines():
    fields=line.split(';');code=int(fields[0],16)
    if fields[1].endswith(', First>'):first=(code,fields);continue
    if fields[1].endswith(', Last>'):
        assert first is not None;begin,record=first;first=None
    else:begin,record=code,fields
    category[record[2]].append((begin,code))
    if record[9]=='Y':binary['Bidi_Mirrored'].append((begin,code))
category={name:norm(value) for name,value in category.items()};assigned=norm([r for name,value in category.items() for r in value if name!='Cn']);category['Cn']=minus([(0,0x10ffff)],assigned)
for group in ['C','L','M','N','P','S','Z']:category[group]=norm([r for name,value in list(category.items()) if len(name)==2 and name.startswith(group) for r in value])
category['LC']=norm(category['Lu']+category['Ll']+category['Lt'])
binary.update(ASCII=[(0,127)],Any=[(0,0x10ffff)],Assigned=assigned)
for name in ['17-DerivedCoreProperties.txt','17-PropList.txt','17-DerivedNormalizationProps.txt','17-extracted-DerivedBinaryProperties.txt','17-emoji-emoji-data.txt']:
    for fields in records(d/name):
        if fields[1] in wanted:binary[fields[1]].append(span(fields[0]))
assert not(wanted-set(binary)),wanted-set(binary)
binary={name:norm(ranges) for name,ranges in binary.items()}
scripts=defaultdict(list)
for fields in records(d/'17-Scripts.txt'):scripts[fields[1]].append(span(fields[0]))
scripts={name:norm(ranges) for name,ranges in scripts.items()};scripts['Unknown']=minus([(0,0x10ffff)],norm([r for ranges in scripts.values() for r in ranges]))
value_alias=defaultdict(dict)
for fields in records(d/'17-PropertyValueAliases.txt'):
    if fields[0] in ['gc','sc']:
        for alias in fields[1:]:
            if alias:value_alias[fields[0]][alias]=fields[1] if fields[0]=='gc' else fields[2]
explicit=[];extensions=defaultdict(list)
for fields in records(d/'17-ScriptExtensions.txt'):
    covered=span(fields[0]);explicit.append(covered)
    for alias in fields[1].split():extensions[value_alias['sc'][alias]].append(covered)
explicit=norm(explicit)
for script in set(scripts)|set(value_alias['sc'].values()):extensions[script]=norm(minus(scripts.get(script,[]),explicit)+extensions[script])
emoji_ranges=defaultdict(list);emoji_strings=defaultdict(set)
for name in ['17-emoji-sequences.txt','17-emoji-zwj-sequences.txt']:
    for fields in records(d/name):
        if fields[1] not in string_names:continue
        points=fields[0].split()
        if len(points)==1:emoji_ranges[fields[1]].append(span(points[0]))
        else:emoji_strings[fields[1]].add(tuple(int(x,16) for x in points))
for name in string_names:
    if name!='RGI_Emoji':emoji_ranges['RGI_Emoji']+=emoji_ranges[name];emoji_strings['RGI_Emoji'].update(emoji_strings[name])
properties=[];aliases={};identity={}
def add(names,ranges,strings=(),is_string=False):
    ranges=tuple(norm(ranges));strings=tuple(sorted(set(strings),key=lambda value:(-len(value),value)));key=(ranges,strings,is_string)
    if key not in identity:identity[key]=len(properties);properties.append(key)
    index=identity[key]
    for name in names:
        assert name not in aliases or aliases[name]==index,(name,aliases.get(name),index)
        aliases[name]=index
for alias,name in allowed.items():add([alias],binary[name])
for alias,name in value_alias['gc'].items():add([alias,'gc='+alias,'General_Category='+alias],category[name])
for alias,name in value_alias['sc'].items():
    if name not in scripts:continue  # Composite ISO15924 codes are not actual Unicode Script values.
    add(['sc='+alias,'Script='+alias],scripts.get(name,[]));add(['scx='+alias,'Script_Extensions='+alias],extensions[name])
for name in string_names:add([name],emoji_ranges[name],emoji_strings[name],True)
flat_ranges=[];flat_strings=[];points=[];property_rows=[]
for ranges,strings,is_string in properties:
    start_range=len(flat_ranges);flat_ranges+=list(ranges);start_string=len(flat_strings)
    for string in strings:flat_strings.append((len(points),len(string)));points+=list(string)
    property_rows.append((start_range,len(ranges),start_string,len(strings),is_string))
lines=['// SPDX-License-Identifier: MIT AND Unicode-3.0','// Generated public Unicode17 ECMAScript property projection.','// Modified data: scripts/generate-native-grep-properties.py; see unicode_data_LICENSE.txt.','#pragma once','#include <array>','#include <cstddef>','#include <cstdint>','#include <string_view>','namespace suitecut::runner::grep_properties {','struct Range {uint32_t first,last;};','struct String {uint32_t first,count;};','struct Property {uint32_t first_range,range_count,first_string,string_count;bool strings;};','struct Alias {std::string_view name;uint32_t property;};']
for name,typ,values,render in [('Ranges','Range',flat_ranges,lambda value:'{0x%x,0x%x}'%value),('Points','uint32_t',points,lambda value:'0x%x'%value),('Strings','String',flat_strings,lambda value:'{%d,%d}'%value),('Properties','Property',property_rows,lambda value:'{%d,%d,%d,%d,%s}'%(*value[:4],'true' if value[4] else 'false')),('Aliases','Alias',sorted(aliases.items()),lambda value:'{"%s",%d}'%value)]:
    lines+=['inline constexpr std::array<'+typ+','+str(len(values))+'> k'+name+'{{']+['  '+render(value)+',' for value in values]+['}};']
lines+=['inline const Property* Find(std::string_view name){','  std::size_t first=0,last=kAliases.size();','  while(first<last){const auto middle=first+(last-first)/2;if(kAliases[middle].name<name)first=middle+1;else last=middle;}','  return first<kAliases.size()&&kAliases[first].name==name?&kProperties[kAliases[first].property]:nullptr;','}','}  // namespace suitecut::runner::grep_properties','']
a.output.write_text('\n'.join(lines));inputs=[d/'UnicodeData-17.0.0.txt',d/'ecma262-property-tables.json',*sorted(d.glob('17-*.txt'))]
a.output.with_suffix('.json').write_text(json.dumps({'version':'17.0.0','properties':len(properties),'aliases':len(aliases),'ranges':len(flat_ranges),'strings':len(flat_strings),'stringPoints':len(points),'sourceHashes':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'generatorSHA256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'outputSHA256':hashlib.sha256(a.output.read_bytes()).hexdigest()},indent=2)+'\n');print(len(properties),'properties',len(flat_ranges),'ranges',len(flat_strings),'strings')
