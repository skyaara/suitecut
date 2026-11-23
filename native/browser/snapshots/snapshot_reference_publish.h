// SPDX-License-Identifier: MIT
#pragma once
// Runs only on a privately authored semantic-capture object in the utility
// context. The native registry supplies exact tokens after real backend validation.
// This mutates native snapshot records only; it never changes author DOM nodes.
inline constexpr char kSuiteCutSnapshotReferencePublish[] = R"JS(function(references){
  if(!Array.isArray(references)||new Set(references).size!==references.length||references.some(ref=>typeof ref!=='string'||!/^s[1-9][0-9]*e(0|[1-9][0-9]*)$/.test(ref)))throw new Error('Invalid native snapshot references');
  if(!this||!Array.isArray(this.forest)||!Array.isArray(this.elements)||!Array.isArray(this.entries))throw new Error('Invalid native snapshot reference capture');
  if(references.length!==this.elements.length)throw new Error('Native snapshot reference count changed');
  const records=new Set(),seenElements=new Set(),seenEntries=new Set(),used=new Set(),pending=[...this.forest];
  while(pending.length){const node=pending.pop();if(!node||typeof node!=='object'||records.has(node)||typeof node.role!=='string')throw new Error('Invalid native snapshot reference forest');records.add(node);if(node.children!==undefined){if(!Array.isArray(node.children))throw new Error('Invalid native snapshot reference children');for(const child of node.children)pending.push(child);}}
  for(const element of this.elements){if(!(element instanceof Element)||element.ownerDocument!==document||seenElements.has(element))throw new Error('Invalid native snapshot reference Element identity');seenElements.add(element);}
  for(const entry of this.entries){if(!Array.isArray(entry)||entry.length!==2||!records.has(entry[0])||seenEntries.has(entry[0])||!Number.isInteger(entry[1])||entry[1]<0||entry[1]>=this.elements.length)throw new Error('Invalid native snapshot reference mapping');seenEntries.add(entry[0]);used.add(entry[1]);}
  if(used.size!==this.elements.length)throw new Error('Incomplete native snapshot reference mapping');
  for(const [node,index]of this.entries)node.ref=references[index];
  return JSON.stringify(this.forest);
})JS";
