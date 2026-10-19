// SPDX-License-Identifier: MIT
#pragma once
// Runs only on a native-owned retained capture in its genuine utility world.
// Child forests are exact JSON strings from other native captures, never DOM
// attributes or author callback results. No author node is modified here.
inline constexpr char kSuiteCutSnapshotReferenceMerge[] = R"JS(function(ref,childJSON){
  if(typeof ref!=='string'||ref.length>192||typeof childJSON!=='string'||childJSON.length>65536||!this||!Array.isArray(this.forest))throw new Error('Invalid native frame snapshot merge');
  const child=JSON.parse(childJSON);if(!Array.isArray(child))throw new Error('Invalid native child snapshot forest');
  let target=null,work=0;const pending=[{nodes:this.forest,depth:0}],seen=new Set();
  while(pending.length){const {nodes,depth}=pending.pop();if(depth>64||nodes.length>4096)throw new Error('Native frame snapshot merge depth exceeded');for(const node of nodes){if(++work>4096||!node||typeof node!=='object'||seen.has(node))throw new Error('Invalid native frame snapshot record');seen.add(node);if(node.ref===ref){if(node.role!=='iframe'||target)throw new Error('Native frame snapshot owner is ambiguous');target=node;}if(node.children!==undefined){if(!Array.isArray(node.children))throw new Error('Invalid native frame snapshot children');pending.push({nodes:node.children,depth:depth+1});}}}
  if(!target)throw new Error('Native frame snapshot owner unavailable');
  // Validate the proposed child tree and combined depth before mutating even
  // these private native records, preserving the previous prepared forest.
  const combined=[{nodes:this.forest,depth:0}];work=0;seen.clear();
  while(combined.length){const {nodes,depth}=combined.pop();if(depth>64||nodes.length>4096)throw new Error('Native aggregate frame depth exceeded');for(const node of nodes){if(++work>4096||!node||typeof node!=='object'||seen.has(node))throw new Error('Invalid native aggregate frame record');seen.add(node);const children=node===target?child:node.children;if(children!==undefined){if(!Array.isArray(children))throw new Error('Invalid native aggregate frame children');combined.push({nodes:children,depth:depth+1});}}}
  const previous=target.children;target.children=child;
  let json;try{json=JSON.stringify(this.forest);if(new TextEncoder().encode(json).byteLength>65536)throw new Error('Native aggregate frame snapshot exceeds 64 KiB');}catch(error){if(previous===undefined)delete target.children;else target.children=previous;throw error;}
  return json;
})JS";
