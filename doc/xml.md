# XML bindings for Lua

The XML module provides simple bindings to the functionality provided by
libXML2.  It is focused on allowing a script to iterate and consume XML
documents where the overall structure is generally known to the
developer.  It also provides some simple document modification
functionality.

The scope of these bindings will increase in future releases.

## Synopsis

    require 'xml'
    doc = xml.parsexml([[<doc><item/></doc>]])

## xml.parsexml(string, keep_blanks)

Parses an XML string and returns an XML document object.
The 2nd argument "keep_blanks" is an optional integer value. 
When not set or set to "0", ignorable white spaces will be ignored, otherwise, 
text nodes containing those blanks will be generated in the DOM output.  

## doc:root()

Returns the root node in the XML document.

## doc:xpath(query [, contextnode])

Performs an XPath query and returns an iterator over the resultant
set of nodes.  You may specify an optional node from the same document
to use as context for the XPath query.

    for node in doc:xpath("//item") do
      print(node:name())
    end

## doc:tostring()

Returns an XML rendition of the document as a string.  This same method
is available as a __tostring metamethod, making the following two lines
equivalent:

    doc:tostring()
    tostring(doc)

## node:doc()

Returns the XML document object which contains the node

## node:unlink()

Unlink a xml node from its xmldoc container. Call this function to avoid the 
xmlnode object been garbage collected when its xmldoc container is gc-ed.
Returns the xml node object:

  doc = xml.parsexml([[<doc><item/></doc>]])
  node = doc:root()
  node = node:unlink()

## node:attr() & node:attribute()

These methods are synonyms, you may use whichever name suits you best.
The attribute function can be used to get or set a specific named
attribute on the node.

    print(node:attr("name")) -- prints the value of the "name" attribute
    node:attr("name", "newval") -- sets the "name" attribute to "newval"
    node:attr("name", nil) -- clears the "name" attribute

## node:addchild(name)

Generates a new node with the specified name and adds it as a child of
node.  Returns the new node.

    kid = node:addchild("childitem")
    kid:attr("name", "newchild!")

## node:addchild(child_node)
Add a node object as a child of node. Returns the added child node.

## node:children()

Returns an iterator that will return each of the child nodes, including
any text nodes that may be present in the document.

    for kid in node:children() do
      print(kid:name())
    end

## node:contents()

Gets or sets the textual contents of the specified node.

    print(node:contents()) -- get contents
    node:contents("Replaced!") -- set contents

## node:name()

Returns the element name of the node.  Returns the string "text" for
text nodes.

## node:tostring()

Returns a string representation of the XML source of the node and its
children.  This same method is also available as a __tostring
metamethod, so the following two lines are equivalent:

    node:tostring()
    tostring(node)

## Attribution

These XML bindings are derived from the Reconnoiter project whose origin
is https://github.com/omniti-labs/reconnoiter.  This source code is used
by Message Systems under the terms of 3-clause BSD style license, which
is reproduced below.  This statement does not modify the terms of your
license agreement with Message Systems.

 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.,  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the following
  disclaimer in the documentation and/or other materials provided
  with the distribution.
* Neither the name OmniTI Computer Consulting, Inc. nor the names
  of its contributors may be used to endorse or promote products
  derived from this software without specific prior written
  permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

