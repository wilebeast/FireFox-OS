/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


/**
   File Name:          12.6.3-10.js
   ECMA Section:       12.6.3 The for...in Statement
   Description:
   The production IterationStatement : for ( LeftHandSideExpression in Expression )
   Statement is evaluated as follows:

   1.  Evaluate the Expression.
   2.  Call GetValue(Result(1)).
   3.  Call ToObject(Result(2)).
   4.  Let C be "normal completion".
   5.  Get the name of the next property of Result(3) that doesn't have the
   DontEnum attribute. If there is no such property, go to step 14.
   6.  Evaluate the LeftHandSideExpression (it may be evaluated repeatedly).
   7.  Call PutValue(Result(6), Result(5)).  PutValue( V, W ):
   1.  If Type(V) is not Reference, generate a runtime error.
   2.  Call GetBase(V).
   3.  If Result(2) is null, go to step 6.
   4.  Call the [[Put]] method of Result(2), passing GetPropertyName(V)
   for the property name and W for the value.
   5.  Return.
   6.  Call the [[Put]] method for the global object, passing
   GetPropertyName(V) for the property name and W for the value.
   7.  Return.
   8.  Evaluate Statement.
   9.  If Result(8) is a value completion, change C to be "normal completion
   after value V" where V is the value carried by Result(8).
   10. If Result(8) is a break completion, go to step 14.
   11. If Result(8) is a continue completion, go to step 5.
   12. If Result(8) is a return completion, return Result(8).
   13. Go to step 5.
   14. Return C.

   Author:             christine@netscape.com
   Date:               11 september 1997
*/
var SECTION = "12.6.3-10";
var VERSION = "ECMA_1";
startTest();
var TITLE   = "The for..in statement";

writeHeaderToLog( SECTION + " "+ TITLE);

//  for ( LeftHandSideExpression in Expression )
//  LeftHandSideExpression:NewExpression:MemberExpression

var count = 0;
function f() {     count++; return new Array("h","e","l","l","o"); }

var result = "";
for ( p in f() ) { result += f()[p] };

new TestCase( SECTION,
	      "count = 0; result = \"\"; "+
	      "function f() { count++; return new Array(\"h\",\"e\",\"l\",\"l\",\"o\"); }"+
	      "for ( p in f() ) { result += f()[p] }; count",
	      6,
	      count );

new TestCase( SECTION,
	      "result",
	      "hello",
	      result );

//  LeftHandSideExpression:NewExpression:MemberExpression [ Expression ]
//  LeftHandSideExpression:NewExpression:MemberExpression . Identifier
//  LeftHandSideExpression:NewExpression:new MemberExpression Arguments
//  LeftHandSideExpression:NewExpression:PrimaryExpression:( Expression )
//  LeftHandSideExpression:CallExpression:MemberExpression Arguments
//  LeftHandSideExpression:CallExpression Arguments
//  LeftHandSideExpression:CallExpression [ Expression ]
//  LeftHandSideExpression:CallExpression . Identifier

test();

