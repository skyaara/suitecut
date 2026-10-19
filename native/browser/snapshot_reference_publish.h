// SPDX-License-Identifier: MIT
#pragma once
// Runs only on a privately authored semantic-capture object in the utility
// context. The native registry supplies exact tokens after real backend validation.
// This mutates native snapshot records only; it never changes author DOM nodes.
inline constexpr char kSuiteCutSnapshotReferencePublish[] = R"JS(function(references){
  if(!Array.isArray(references)||references.length>4096||new Set(references).size!==references.length||references.some(ref=>typeof ref!=='string'||ref.length>192||!/^r[A-Za-z0-9]{1,64}_[1-9][0-9]*_(0|[1-9][0-9]*)$/.test(ref)))throw new Error('Invalid native snapshot references');
  if(!this||!Array.isArray(this.forest)||!Array.isArray(this.elements)||!Array.isArray(this.entries)||this.forest.length>4096||this.elements.length>4096||this.entries.length>4096)throw new Error('Invalid native snapshot reference capture');
  if(references.length!==this.elements.length)throw new Error('Native snapshot reference count changed');
  const records=new Set(),seenElements=new Set(),seenEntries=new Set(),used=new Set(),pending=[...this.forest];let count=0,work=0;
  while(pending.length){if(++work>16384)throw new Error('Native snapshot reference publication work limit exceeded');const node=pending.pop();if(!node||typeof node!=='object'||records.has(node)||typeof node.role!=='string'||++count>4096)throw new Error('Invalid native snapshot reference forest');records.add(node);if(node.children!==undefined){if(!Array.isArray(node.children)||node.children.length>4096)throw new Error('Invalid native snapshot reference children');for(const child of node.children)pending.push(child);}}
  for(const element of this.elements){if(!(element instanceof Element)||element.ownerDocument!==document||seenElements.has(element))throw new Error('Invalid native snapshot reference Element identity');seenElements.add(element);}
  for(const entry of this.entries){if(!Array.isArray(entry)||entry.length!==2||!records.has(entry[0])||seenEntries.has(entry[0])||!Number.isInteger(entry[1])||entry[1]<0||entry[1]>=this.elements.length)throw new Error('Invalid native snapshot reference mapping');seenEntries.add(entry[0]);used.add(entry[1]);}
  if(used.size!==this.elements.length)throw new Error('Incomplete native snapshot reference mapping');
  for(const [node,index]of this.entries)node.ref=references[index];
  const json=JSON.stringify(this.forest);if(new TextEncoder().encode(json).byteLength>65536)throw new Error('Native snapshot reference forest exceeds 64 KiB');return json;
})JS";
