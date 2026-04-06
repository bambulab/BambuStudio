//////////////////////////////////////////////////////////////////////
// LibFile: math.scad
//   Math helper functions.
//   To use, add the following lines to the beginning of your file:
//   ```
//   use <BOSL/math.scad>
//   ```
//////////////////////////////////////////////////////////////////////

/*
BSD 2-Clause License

Copyright (c) 2017, Revar Desmera
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


include <constants.scad>
include <compat.scad>


// Section: Math Constants

PHI = (1+sqrt(5))/2;  // The golden ratio phi.

EPSILON = 1e-9;  // A really small value useful in comparing FP numbers.  ie: abs(a-b)<EPSILON



// Function: Cpi()
// Status: DEPRECATED, use `PI` instead.
// Description:
//   Returns the value of pi.
function Cpi() = PI;  // Deprecated!  Use the variable PI instead.


// Section: Simple Calculations

// Function: quant()
// Description:
//   Quantize a value `x` to an integer multiple of `y`, rounding to the nearest multiple.
// Arguments:
//   x = The value to quantize.
//   y = The multiple to quantize to.
function quant(x,y) = floor(x/y+0.5)*y;


// Function: quantdn()
// Description:
//   Quantize a value `x` to an integer multiple of `y`, rounding down to the previous multiple.
// Arguments:
//   x = The value to quantize.
//   y = The multiple to quantize to.
function quantdn(x,y) = floor(x/y)*y;


// Function: quantup()
// Description:
//   Quantize a value `x` to an integer multiple of `y`, rounding up to the next multiple.
// Arguments:
//   x = The value to quantize.
//   y = The multiple to quantize to.
function quantup(x,y) = ceil(x/y)*y;


// Function: constrain()
// Usage:
//   constrain(v, minval, maxval);
// Description:
//   Constrains value to a range of values between minval and maxval, inclusive.
// Arguments:
//   v = value to constrain.
//   minval = minimum value to return, if out of range.
//   maxval = maximum value to return, if out of range.
function constrain(v, minval, maxval) = min(maxval, max(minval, v));


// Function: min_index()
// Usage:
//   min_index(vals);
// Description:
//   Returns the index of the minimal value in the given list.
function min_index(vals, _minval, _minidx, _i=0) =
	_i>=len(vals)? _minidx :
	min_index(
		vals,
		((_minval == undef || vals[_i] < _minval)? vals[_i] : _minval),
		((_minval == undef || vals[_i] < _minval)? _i : _minidx),
		_i+1
	);


// Function: max_index()
// Usage:
//   max_index(vals);
// Description:
//   Returns the index of the maximum value in the given list.
function max_index(vals, _maxval, _maxidx, _i=0) =
	_i>=len(vals)? _maxidx :
	max_index(
		vals,
		((_maxval == undef || vals[_i] > _maxval)? vals[_i] : _maxval),
		((_maxval == undef || vals[_i] > _maxval)? _i : _maxidx),
		_i+1
	);


// Function: posmod()
// Usage:
//   posmod(x,m)
// Description:
//   Returns the positive modulo `m` of `x`.  Value returned will be in the range 0 ... `m`-1.
//   This if useful for normalizing angles to 0 ... 360.
// Arguments:
//   x = The value to constrain.
//   m = Modulo value.
function posmod(x,m) = (x%m+m)%m;


// Function: modrange()
// Usage:
//   modrange(x, y, m, [step])
// Description:
//   Returns a normalized list of values from `x` to `y`, by `step`, modulo `m`.  Wraps if `x` > `y`.
// Arguments:
//   x = The start value to constrain.
//   y = The end value to constrain.
//   m = Modulo value.
//   step = Step by this amount.
// Examples:
//   modrange(90,270,360, step=45);   // Outputs [90,135,180,225,270]
//   modrange(270,90,360, step=45);   // Outputs [270,315,0,45,90]
//   modrange(90,270,360, step=-45);  // Outputs [90,45,0,315,270]
//   modrange(270,90,360, step=-45);  // Outputs [270,225,180,135,90]
function modrange(x, y, m, step=1) =
	let(
		a = posmod(x, m),
		b = posmod(y, m),
		c = step>0? (a>b? b+m : b) : (a<b? b-m : b)
	) [for (i=[a:step:c]) (i%m+m)%m];


// Function: gaussian_rand()
// Usage:
//   gaussian_rand(mean, stddev)
// Description:
//   Returns a random number with a gaussian/normal distribution.
// Arguments:
//   mean = The average random number returned.
//   stddev = The standard deviation of the numbers to be returned.
function gaussian_rand(mean, stddev) = let(s=rands(0,1,2)) mean + stddev*sqrt(-2*ln(s.x))*cos(360*s.y);


// Function: log_rand()
// Usage:
//   log_rand(minval, maxval, factor);
// Description:
//   Returns a single random number, with a logarithmic distribution.
// Arguments:
//   minval = Minimum value to return.
//   maxval = Maximum value to return.  `minval` <= X < `maxval`.
//   factor = Log factor to use.  Values of X are returned `factor` times more often than X+1.
function log_rand(minval, maxval, factor) = -ln(1-rands(1-1/pow(factor,minval), 1-1/pow(factor,maxval), 1)[0])/ln(factor);


// Function: segs()
// Description:
//   Calculate the standard number of sides OpenSCAD would give a circle based on `$fn`, `$fa`, and `$fs`.
// Arguments:
//   r = Radius of circle to get the number of segments for.
function segs(r) =
	$fn>0? ($fn>3? $fn : 3) :
	ceil(max(5, min(360/$fa, abs(r)*2*PI/$fs)));


// Function: lerp()
// Description: Interpolate between two values or vectors.
// Arguments:
//   a = First value.
//   b = Second value.
//   u = The proportion from `a` to `b` to calculate.  Valid range is 0.0 to 1.0, inclusive.
function lerp(a,b,u) = (1-u)*a + u*b;


// Function: hypot()
// Description: Calculate hypotenuse length of a 2D or 3D triangle.
// Arguments:
//   x = Length on the X axis.
//   y = Length on the Y axis.
//   z = Length on the Z axis.
function hypot(x,y,z=0) = norm([x,y,z]);


// Function: hypot3()
// Status: DEPRECATED, use `norm([x,y,z])` instead.
// Description: Calculate hypotenuse length of 3D triangle.
// Arguments:
//   x = Length on the X axis.
//   y = Length on the Y axis.
//   z = Length on the Z axis.
function hypot3(x,y,z) = norm([x,y,z]);


// Function: distance()
// Status: DEPRECATED, use `norm(p2-p1)` instead.  It's shorter.
// Description: Returns the distance between a pair of 2D or 3D points.
function distance(p1, p2) = norm(point3d(p2)-point3d(p1));


// Function: sinh()
// Description: Takes a value `x`, and returns the hyperbolic sine of it.
function sinh(x) = (exp(x)-exp(-x))/2;


// Function: cosh()
// Description: Takes a value `x`, and returns the hyperbolic cosine of it.
function cosh(x) = (exp(x)+exp(-x))/2;


// Function: tanh()
// Description: Takes a value `x`, and returns the hyperbolic tangent of it.
function tanh(x) = sinh(x)/cosh(x);


// Function: asinh()
// Description: Takes a value `x`, and returns the inverse hyperbolic sine of it.
function asinh(x) = ln(x+sqrt(x*x+1));


// Function: acosh()
// Description: Takes a value `x`, and returns the inverse hyperbolic cosine of it.
function acosh(x) = ln(x+sqrt(x*x-1));


// Function: atanh()
// Description: Takes a value `x`, and returns the inverse hyperbolic tangent of it.
function atanh(x) = ln((1+x)/(1-x))/2;


// Function: sum()
// Description:
//   Returns the sum of all entries in the given array.
//   If passed an array of vectors, returns a vector of sums of each part.
// Arguments:
//   v = The vector to get the sum of.
// Example:
//   sum([1,2,3]);  // returns 6.
//   sum([[1,2,3], [3,4,5], [5,6,7]]);  // returns [9, 12, 15]
function sum(v, i=0, tot=undef) = i>=len(v)? tot : sum(v, i+1, ((tot==undef)? v[i] : tot+v[i]));


// Function: sum_of_squares()
// Description:
//   Returns the sum of the square of each element of a vector.
// Arguments:
//   v = The vector to get the sum of.
// Example:
//   sum_of_squares([1,2,3]);  // returns 14.
function sum_of_squares(v, i=0, tot=0) = sum(vmul(v,v));


// Function: sum_of_sines()
// Usage:
//   sum_of_sines(a,sines)
// Description:
//   Gives the sum of a series of sines, at a given angle.
// Arguments:
//   a = Angle to get the value for.
//   sines = List of [amplitude, frequency, offset] items, where the frequency is the number of times the cycle repeats around the circle.
function sum_of_sines(a, sines) =
	sum([
		for (s = sines) let(
			ss=point3d(s),
			v=ss.x*sin(a*ss.y+ss.z)
		) v
	]);


// Function: mean()
// Description:
//   Returns the mean of all entries in the given array.
//   If passed an array of vectors, returns a vector of mean of each part.
// Arguments:
//   v = The list of values to get the mean of.
// Example:
//   mean([2,3,4]);  // returns 3.
//   mean([[1,2,3], [3,4,5], [5,6,7]]);  // returns [3, 4, 5]
function mean(v) = sum(v)/len(v);


// Section: Comparisons and Logic


// Function: compare_vals()
// Usage:
//   compare_vals(a, b);
// Description:
//   Compares two values.  Lists are compared recursively.
//   Results are undefined if the two values are not of similar types.
// Arguments:
//   a = First value to compare.
//   b = Second value to compare.
function compare_vals(a, b) =
	(a==b)? 0 :
	(a==undef)? -1 :
	(b==undef)? 1 :
	((a==[] || a=="" || a[0]!=undef) && (b==[] || b=="" || b[0]!=undef))? (
		compare_lists(a, b)
	) : (a<b)? -1 :
	(a>b)? 1 : 0;


// Function: compare_lists()
// Usage:
//   compare_lists(a, b)
// Description:
//   Compare contents of two lists.
//   Returns <0 if `a`<`b`.
//   Returns 0 if `a`==`b`.
//   Returns >0 if `a`>`b`.
//   Results are undefined if elements are not of similar types.
// Arguments:
//   a = First list to compare.
//   b = Second list to compare.
function compare_lists(a, b, n=0) =
	let(
		// This curious construction enables tail recursion optimization.
		cmp = (a==b)? 0 :
			(len(a)<=n)? -1 :
			(len(b)<=n)? 1 :
			(a==a[n] || b==b[n])? (
				a<b? -1 : a>b? 1 : 0
			) : compare_vals(a[n], b[n])
	)
	(cmp != 0 || a==b)? cmp :
	compare_lists(a, b, n+1);


// Function: any()
// Description:
//   Returns true if any item in list `l` evaluates as true.
//   If `l` is a lists of lists, `any()` is applied recursively to each sublist.
// Arguments:
//   l = The list to test for true items.
// Example:
//   any([0,false,undef]);  // Returns false.
//   any([1,false,undef]);  // Returns true.
//   any([1,5,true]);       // Returns true.
//   any([[0,0], [0,0]]);   // Returns false.
//   any([[0,0], [1,0]]);   // Returns true.
function any(l, i=0, succ=false) =
	(i>=len(l) || succ)? succ :
	any(
		l, i=i+1, succ=(
			is_array(l[i])? any(l[i]) :
			!(!l[i])
		)
	);


// Function: all()
// Description:
//   Returns true if all items in list `l` evaluate as true.
//   If `l` is a lists of lists, `all()` is applied recursively to each sublist.
// Arguments:
//   l = The list to test for true items.
// Example:
//   all([0,false,undef]);  // Returns false.
//   all([1,false,undef]);  // Returns false.
//   all([1,5,true]);       // Returns true.
//   all([[0,0], [0,0]]);   // Returns false.
//   all([[0,0], [1,0]]);   // Returns false.
//   all([[1,1], [1,1]]);   // Returns true.
function all(l, i=0, fail=false) =
	(i>=len(l) || fail)? (!fail) :
	all(
		l, i=i+1, fail=(
			is_array(l[i])? !all(l[i]) :
			!l[i]
		)
	);


// Function: count_true()
// Usage:
//   count_true(l)
// Description:
//   Returns the number of items in `l` that evaluate as true.
//   If `l` is a lists of lists, this is applied recursively to each
//   sublist.  Returns the total count of items that evaluate as true
//   in all recursive sublists.
// Arguments:
//   l = The list to test for true items.
//   nmax = If given, stop counting if `nmax` items evaluate as true.
// Example:
//   count_true([0,false,undef]);  // Returns 0.
//   count_true([1,false,undef]);  // Returns 1.
//   count_true([1,5,false]);      // Returns 2.
//   count_true([1,5,true]);       // Returns 3.
//   count_true([[0,0], [0,0]]);   // Returns 0.
//   count_true([[0,0], [1,0]]);   // Returns 1.
//   count_true([[1,1], [1,1]]);   // Returns 4.
//   count_true([[1,1], [1,1]], nmax=3);  // Returns 3.
function count_true(l, nmax=undef, i=0, cnt=0) =
	(i>=len(l) || (nmax!=undef && cnt>=nmax))? cnt :
	count_true(
		l=l, nmax=nmax, i=i+1, cnt=cnt+(
			is_array(l[i])? count_true(l[i], nmax=nmax-cnt) :
			(l[i]? 1 : 0)
		)
	);



// Section: List/Array Operations


// Function: cdr()
// Status: DEPRECATED, use `slice(list,1,-1)` instead.
// Description: Returns all but the first item of a given array.
// Arguments:
//   list = The list to get the tail of.
function cdr(list) = len(list)<=1? [] : [for (i=[1:len(list)-1]) list[i]];


// Function: replist()
// Usage:
//   replist(val, n)
// Description:
//   Generates a list or array of `n` copies of the given `list`.
//   If the count `n` is given as a list of counts, then this creates a
//   multi-dimensional array, filled with `val`.
// Arguments:
//   val = The value to repeat to make the list or array.
//   n = The number of copies to make of `val`.
// Example:
//   replist(1, 4);        // Returns [1,1,1,1]
//   replist(8, [2,3]);    // Returns [[8,8,8], [8,8,8]]
//   replist(0, [2,2,3]);  // Returns [[[0,0,0],[0,0,0]], [[0,0,0],[0,0,0]]]
//   replist([1,2,3],3);   // Returns [[1,2,3], [1,2,3], [1,2,3]]
function replist(val, n, i=0) =
	is_scalar(n)? [for(j=[1:n]) val] :
	(i>=len(n))? val :
	[for (j=[1:n[i]]) replist(val, n, i+1)];


// Function: in_list()
// Description: Returns true if value `x` is in list `l`.
// Arguments:
//   x = The value to search for.
//   l = The list to search.
//   idx = If given, searches the given subindexes for matches for `x`.
// Example:
//   in_list("bar", ["foo", "bar", "baz"]);  // Returns true.
//   in_list("bee", ["foo", "bar", "baz"]);  // Returns false.
//   in_list("bar", [[2,"foo"], [4,"bar"], [3,"baz"]], idx=1);  // Returns true.
function in_list(x,l,idx=undef) = search([x], l, num_returns_per_match=1, index_col_num=idx) != [[]];


// Function: slice()
// Description:
//   Returns a slice of a list.  The first item is index 0.
//   Negative indexes are counted back from the end.  The last item is -1.
// Arguments:
//   arr = The array/list to get the slice of.
//   st = The index of the first item to return.
//   end = The index after the last item to return, unless negative, in which case the last item to return.
// Example:
//   slice([3,4,5,6,7,8,9], 3, 5);   // Returns [6,7]
//   slice([3,4,5,6,7,8,9], 2, -1);  // Returns [5,6,7,8,9]
//   slice([3,4,5,6,7,8,9], 1, 1);   // Returns []
//   slice([3,4,5,6,7,8,9], 6, -1);  // Returns [9]
//   slice([3,4,5,6,7,8,9], 2, -2);  // Returns [5,6,7,8]
function slice(arr,st,end) = let(
		s=st<0?(len(arr)+st):st,
		e=end<0?(len(arr)+end+1):end
	) (s==e)? [] : [for (i=[s:e-1]) if (e>s) arr[i]];


// Function: wrap_range()
// Status: DEPRECATED, use `select()` instead.
// Description:
//   Returns a portion of a list, wrapping around past the beginning, if end<start. 
//   The first item is index 0. Negative indexes are counted back from the end.
//   The last item is -1.  If only the `start` index is given, returns just the value
//   at that position.
// Usage:
//   wrap_range(list,start)
//   wrap_range(list,start,end)
// Arguments:
//   list = The list to get the portion of.
//   start = The index of the first item.
//   end = The index of the last item.
function wrap_range(list, start, end=undef) = select(list,start,end);


// Function: select()
// Description:
//   Returns a portion of a list, wrapping around past the beginning, if end<start. 
//   The first item is index 0. Negative indexes are counted back from the end.
//   The last item is -1.  If only the `start` index is given, returns just the value
//   at that position.
// Usage:
//   select(list,start)
//   select(list,start,end)
// Arguments:
//   list = The list to get the portion of.
//   start = The index of the first item.
//   end = The index of the last item.
// Example:
//   l = [3,4,5,6,7,8,9];
//   select(l, 5, 6);   // Returns [8,9]
//   select(l, 5, 8);   // Returns [8,9,3,4]
//   select(l, 5, 2);   // Returns [8,9,3,4,5]
//   select(l, -3, -1); // Returns [7,8,9]
//   select(l, 3, 3);   // Returns [6]
//   select(l, 4);      // Returns 7
//   select(l, -2);     // Returns 8
//   select(l, [1:3]);  // Returns [4,5,6]
//   select(l, [1,3]);  // Returns [4,6]
function select(list, start, end=undef) =
	let(l=len(list))
	(list==[])? [] :
	end==undef? (
		is_scalar(start)?
			let(s=(start%l+l)%l) list[s] :
			[for (i=start) list[(i%l+l)%l]]
	) : (
		let(s=(start%l+l)%l, e=(end%l+l)%l)
		(s<=e)?
			[for (i = [s:e]) list[i]] :
			concat([for (i = [s:l-1]) list[i]], [for (i = [0:e]) list[i]])
	);


// Function: reverse()
// Description: Reverses a list/array.
// Arguments:
//   list = The list to reverse.
// Example:
//   reverse([3,4,5,6]);  // Returns [6,5,4,3]
function reverse(list) = [ for (i = [len(list)-1 : -1 : 0]) list[i] ];


// Function: array_subindex()
// Description:
//   For each array item, return the indexed subitem.
//   Returns a list of the values of each vector at the specfied
//   index list or range.  If the index list or range has
//   only one entry the output list is flattened.  
// Arguments:
//   v = The given list of lists.
//   idx = The index, list of indices, or range of indices to fetch.
// Example:
//   v = [[[1,2,3,4],[5,6,7,8],[9,10,11,12],[13,14,15,16]];
//   array_subindex(v,2);      // Returns [3, 7, 11, 15]
//   array_subindex(v,[2,1]);  // Returns [[3, 2], [7, 6], [11, 10], [15, 14]]
//   array_subindex(v,[1:3]);  // Returns [[2, 3, 4], [6, 7, 8], [10, 11, 12], [14, 15, 16]]
function array_subindex(v, idx) = [
	for(val=v) let(value=[for(i=idx) val[i]])
		len(value)==1 ? value[0] : value
];


// Function: list_range()
// Usage:
//   list_range(n, [s], [e], [step])
//   list_range(e, [step])
//   list_range(s, e, [step])
// Description:
//   Returns a list, counting up from starting value `s`, by `step` increments,
//   until either `n` values are in the list, or it reaches the end value `e`.
// Arguments:
//   n = Desired number of values in returned list, if given.
//   s = Starting value.  Default: 0
//   e = Ending value to stop at, if given.
//   step = Amount to increment each value.  Default: 1
// Example:
//   list_range(4);                  // Returns [0,1,2,3]
//   list_range(n=4, step=2);        // Returns [0,2,4,6]
//   list_range(n=4, s=3, step=3);   // Returns [3,6,9,12]
//   list_range(n=5, s=0, e=10);     // Returns [0, 2.5, 5, 7.5, 10]
//   list_range(e=3);                // Returns [0,1,2,3]
//   list_range(e=6, step=2);        // Returns [0,2,4,6]
//   list_range(s=3, e=5);           // Returns [3,4,5]
//   list_range(s=3, e=8, step=2);   // Returns [3,5,7]
//   list_range(s=4, e=8, step=2);   // Returns [4,6,8]
//   list_range(n=4, s=[3,4], step=[2,3]);  // Returns [[3,4], [5,7], [7,10], [9,13]]
function list_range(n=undef, s=0, e=undef, step=1) =
	(n!=undef && e!=undef)? [for (i=[0:1:n-1]) s+(e-s)*i/(n-1)] :
	(n!=undef)? [for (i=[0:n-1]) let(v=s+step*i) v] :
	(e!=undef)? [for (v=[s:step:e]) v] :
	assertion(false, "Must supply one of `n` or `e`.");


// Function: array_shortest()
// Description:
//   Returns the length of the shortest sublist in a list of lists.
// Arguments:
//   vecs = A list of lists.
function array_shortest(vecs) = min([for (v = vecs) len(v)]);


// Function: array_longest()
// Description:
//   Returns the length of the longest sublist in a list of lists.
// Arguments:
//   vecs = A list of lists.
function array_longest(vecs) = max([for (v = vecs) len(v)]);


// Function: array_pad()
// Description:
//   If the list `v` is shorter than `minlen` length, pad it to length with the value given in `fill`.
// Arguments:
//   v = A list.
//   minlen = The minimum length to pad the list to.
//   fill = The value to pad the list with.
function array_pad(v, minlen, fill=undef) = let(l=len(v)) [for (i=[0:max(l,minlen)-1]) i<l? v[i] : fill];


// Function: array_trim()
// Description:
//   If the list `v` is longer than `maxlen` length, truncates it to be `maxlen` items long.
// Arguments:
//   v = A list.
//   minlen = The minimum length to pad the list to.
function array_trim(v, maxlen) = maxlen<1? [] : [for (i=[0:min(len(v),maxlen)-1]) v[i]];


// Function: array_fit()
// Description:
//   If the list `v` is longer than `length` items long, truncates it to be exactly `length` items long.
//   If the list `v` is shorter than `length` items long, pad it to length with the value given in `fill`.
// Arguments:
//   v = A list.
//   minlen = The minimum length to pad the list to.
//   fill = The value to pad the list with.
function array_fit(v, length, fill) = let(l=len(v)) (l==length)? v : (l>length)? array_trim(v,length) : array_pad(v,length,fill);


// Function: enumerate()
// Description:
//   Returns a list, with each item of the given list `l` numbered in a sublist.
//   Something like: `[[0,l[0]], [1,l[1]], [2,l[2]], ...]`
// Arguments:
//   l = List to enumerate.
//   idx = If given, enumerates just the given subindex items of `l`.
// Example:
//   enumerate(["a","b","c"]);  // Returns: [[0,"a"], [1,"b"], [2,"c"]]
//   enumerate([[88,"a"],[76,"b"],[21,"c"]], idx=1);  // Returns: [[0,"a"], [1,"b"], [2,"c"]]
//   enumerate([["cat","a",12],["dog","b",10],["log","c",14]], idx=[1:2]);  // Returns: [[0,"a",12], [1,"b",10], [2,"c",14]]
function enumerate(l,idx=undef) =
	(l==[])? [] :
	(idx==undef)?
		[for (i=[0:len(l)-1]) [i,l[i]]] :
		[for (i=[0:len(l)-1]) concat([i], [for (j=idx) l[i][j]])];


// Function: array_zip()
// Usage:
//   array_zip(v1, v2, v3, [fit], [fill]);
//   array_zip(vecs, [fit], [fill]);
// Description:
//   Zips together corresponding items from two or more lists.
//   Returns a list of lists, where each sublist contains corresponding
//   items from each of the input lists.  `[[A1, B1, C1], [A2, B2, C2], ...]`
// Arguments:
//   vecs = A list of two or more lists to zipper together.
//   fit = If `fit=="short"`, the zips together up to the length of the shortest list in vecs.  If `fit=="long"`, then pads all lists to the length of the longest, using the value in `fill`.  If `fit==false`, then requires all lists to be the same length.  Default: false.
//   fill = The default value to fill in with if one or more lists if short.  Default: undef
// Example:
//   v1 = [1,2,3,4];
//   v2 = [5,6,7];
//   v3 = [8,9,10,11];
//   array_zip(v1,v3);                       // returns [[1,8], [2,9], [3,10], [4,11]]
//   array_zip([v1,v3]);                     // returns [[1,8], [2,9], [3,10], [4,11]]
//   array_zip([v1,v2], fit="short");        // returns [[1,5], [2,6], [3,7]]
//   array_zip([v1,v2], fit="long");         // returns [[1,5], [2,6], [3,7], [4,undef]]
//   array_zip([v1,v2], fit="long, fill=0);  // returns [[1,5], [2,6], [3,7], [4,0]]
//   array_zip([v1,v2,v3], fit="long");      // returns [[1,5,8], [2,6,9], [3,7,10], [4,undef,11]]
// Example:
//   v1 = [[1,2,3], [4,5,6], [7,8,9]];
//   v2 = [[20,19,18], [17,16,15], [14,13,12]];
//   array_zip(v1,v2);    // Returns [[1,2,3,20,19,18], [4,5,6,17,16,15], [7,8,9,14,13,12]]
function array_zip(vecs, v2, v3, fit=false, fill=undef) =
	(v3!=undef)? array_zip([vecs,v2,v3], fit=fit, fill=fill) :
	(v2!=undef)? array_zip([vecs,v2], fit=fit, fill=fill) :
	let(
		dummy1 = assert_in_list("fit", fit, [false, "short", "long"]),
		minlen = array_shortest(vecs),
		maxlen = array_longest(vecs),
		dummy2 = (fit==false)? assertion(minlen==maxlen, "Input vectors must have the same length") : 0
	) (fit == "long")?
		[for(i=[0:maxlen-1]) [for(v=vecs) for(x=(i<len(v)? v[i] : (fill==undef)? [fill] : fill)) x] ] :
		[for(i=[0:minlen-1]) [for(v=vecs) for(x=v[i]) x] ];



// Function: array_group()
// Description:
//   Takes a flat array of values, and groups items in sets of `cnt` length.
//   The opposite of this is `flatten()`.
// Arguments:
//   v = The list of items to group.
//   cnt = The number of items to put in each grouping.
//   dflt = The default value to fill in with is the list is not a multiple of `cnt` items long.
// Example:
//   v = [1,2,3,4,5,6];
//   array_group(v,2) returns [[1,2], [3,4], [5,6]]
//   array_group(v,3) returns [[1,2,3], [4,5,6]]
//   array_group(v,4,0) returns [[1,2,3,4], [5,6,0,0]]
function array_group(v, cnt=2, dflt=0) = [for (i = [0:cnt:len(v)-1]) [for (j = [0:cnt-1]) default(v[i+j], dflt)]];


// Function: flatten()
// Description: Takes a list of lists and flattens it by one level.
// Arguments:
//   l = List to flatten.
// Example:
//   flatten([[1,2,3], [4,5,[6,7,8]]]) returns [1,2,3,4,5,[6,7,8]]
function flatten(l) = [for (a = l) for (b = a) b];


// Function: sort()
// Usage:
//   sort(arr, [idx])
// Description:
//   Sorts the given list using `compare_vals()`.  Results are undefined if list elements are not of similar type.
// Arguments:
//   arr = The list to sort.
//   idx = If given, the index, range, or list of indices of sublist items to compare.
// Example:
//   l = [45,2,16,37,8,3,9,23,89,12,34];
//   sorted = sort(l);  // Returns [2,3,8,9,12,16,23,34,37,45,89]
function sort(arr, idx=undef) =
	(len(arr)<=1) ? arr :
	let(
		pivot = arr[floor(len(arr)/2)],
		pivotval = idx==undef? pivot : [for (i=idx) pivot[i]],
		compare = [
			for (entry = arr) let(
				val = idx==undef? entry : [for (i=idx) entry[i]],
				cmp = compare_vals(val, pivotval)
			) cmp
		],
		lesser  = [ for (i = [0:len(arr)-1]) if (compare[i] < 0) arr[i] ],
		equal   = [ for (i = [0:len(arr)-1]) if (compare[i] ==0) arr[i] ],
		greater = [ for (i = [0:len(arr)-1]) if (compare[i] > 0) arr[i] ]
	)
	concat(sort(lesser,idx), equal, sort(greater,idx));


// Function: sortidx()
// Description:
//   Given a list, calculates the sort order of the list, and returns
//   a list of indexes into the original list in that sorted order.
//   If you iterate the returned list in order, and use the list items
//   to index into the original list, you will be iterating the original
//   values in sorted order.
// Example:
//   lst = ["d","b","e","c"];
//   idxs = sortidx(lst);  // Returns: [1,3,0,2]
//   ordered = [for (i=idxs) lst[i]];  // Returns: ["b", "c", "d", "e"]
// Example:
//   lst = [
//   	["foo", 88, [0,0,1], false],
//   	["bar", 90, [0,1,0], true],
//   	["baz", 89, [1,0,0], false],
//   	["qux", 23, [1,1,1], true]
//   ];
//   idxs1 = sortidx(lst, idx=1); // Returns: [3,0,2,1]
//   idxs2 = sortidx(lst, idx=0); // Returns: [1,2,0,3]
//   idxs3 = sortidx(lst, idx=[1,3]); // Returns: [3,0,2,1]
function sortidx(l, idx=undef) =
	(l==[])? [] :
	let(
		ll=enumerate(l,idx=idx),
		sidx = [1:len(ll[0])-1]
	)
	array_subindex(sort(ll, idx=sidx), 0);


// Function: unique()
// Usage:
//   unique(arr);
// Description:
//   Returns a sorted list with all repeated items removed.
// Arguments:
//   arr = The list to uniquify.
function unique(arr) =
	len(arr)<=1? arr : let(
		sorted = sort(arr)
	) [
		for (i=[0:len(sorted)-1])
			if (i==0 || (sorted[i] != sorted[i-1]))
				sorted[i]
	];



// Function: list_remove()
// Usage:
//   list_remove(list, elements)
// Description:
//   Remove all items from `list` whose indexes are in `elements`.
// Arguments:
//   list = The list to remove items from.
//   elements = The list of indexes of items to remove.
function list_remove(list, elements) = [
	for (i = [0:len(list)-1]) if (!search(i, elements)) list[i]
];



// Internal.  Not exposed.
function _array_dim_recurse(v) =
    !is_list(v[0])?  (
		sum( [for(entry=v) is_list(entry) ? 1 : 0]) == 0 ? [] : [undef]
	) : let(
		firstlen = len(v[0]),
		first = sum( [for(entry = v) len(entry) == firstlen  ? 0 : 1]   ) == 0 ? firstlen : undef,
		leveldown = flatten(v)
	) is_list(leveldown[0])? (
		concat([first],_array_dim_recurse(leveldown))
	) : [first];


// Function: array_dim()
// Usage:
//   array_dim(v, [depth])
// Description:
//   Returns the size of a multi-dimensional array.  Returns a list of
//   dimension lengths.  The length of `v` is the dimension `0`.  The
//   length of the items in `v` is dimension `1`.  The length of the
//   items in the items in `v` is dimension `2`, etc.  For each dimension,
//   if the length of items at that depth is inconsistent, `undef` will
//   be returned.  If no items of that dimension depth exist, `0` is
//   returned.  Otherwise, the consistent length of items in that
//   dimensional depth is returned.
// Arguments:
//   v = Array to get dimensions of.
//   depth = Dimension to get size of.  If not given, returns a list of dimension lengths.
// Examples:
//   array_dim([[[1,2,3],[4,5,6]],[[7,8,9],[10,11,12]]]);     // Returns [2,2,3]
//   array_dim([[[1,2,3],[4,5,6]],[[7,8,9],[10,11,12]]], 0);  // Returns 2
//   array_dim([[[1,2,3],[4,5,6]],[[7,8,9],[10,11,12]]], 2);  // Returns 3
//   array_dim([[[1,2,3],[4,5,6]],[[7,8,9]]]);                // Returns [2,undef,3]
function array_dim(v, depth=undef) =
	(depth == undef)? (
		concat([len(v)], _array_dim_recurse(v))
	) : (depth == 0)? (
		len(v)
	) : (
		let(dimlist = _array_dim_recurse(v))
		(depth > len(dimlist))? 0 : dimlist[depth-1]
	);



// Section: Vector Manipulation

// Function: vmul()
// Description:
//   Element-wise vector multiplication.  Multiplies each element of vector `v1` by
//   the corresponding element of vector `v2`.  Returns a vector of the products.
// Arguments:
//   v1 = The first vector.
//   v2 = The second vector.
// Example:
//   vmul([3,4,5], [8,7,6]);  // Returns [24, 28, 30]
function vmul(v1, v2) = [for (i = [0:len(v1)-1]) v1[i]*v2[i]];


// Function: vdiv()
// Description:
//   Element-wise vector division.  Divides each element of vector `v1` by
//   the corresponding element of vector `v2`.  Returns a vector of the quotients.
// Arguments:
//   v1 = The first vector.
//   v2 = The second vector.
// Example:
//   vdiv([24,28,30], [8,7,6]);  // Returns [3, 4, 5]
function vdiv(v1, v2) = [for (i = [0:len(v1)-1]) v1[i]/v2[i]];


// Function: vabs()
// Description: Returns a vector of the absolute value of each element of vector `v`.
// Arguments:
//   v = The vector to get the absolute values of.
function vabs(v) = [for (x=v) abs(x)];


// Function: normalize()
// Description:
//   Returns unit length normalized version of vector v.
// Arguments:
//   v = The vector to normalize.
function normalize(v) = v/norm(v);


// Function: vector2d_angle()
// Status: DEPRECATED, use `vector_angle()` instead.
// Usage:
//   vector2d_angle(v1,v2);
// Description:
//   Returns angle in degrees between two 2D vectors.
// Arguments:
//   v1 = First 2D vector.
//   v2 = Second 2D vector.
function vector2d_angle(v1,v2) = vector_angle(v1,v2);


// Function: vector3d_angle()
// Status: DEPRECATED, use `vector_angle()` instead.
// Usage:
//   vector3d_angle(v1,v2);
// Description:
//   Returns angle in degrees between two 3D vectors.
// Arguments:
//   v1 = First 3D vector.
//   v2 = Second 3D vector.
function vector3d_angle(v1,v2) = vector_angle(v1,v2);


// Function: vector_angle()
// Usage:
//   vector_angle(v1,v2);
// Description:
//   Returns angle in degrees between two vectors of similar dimensions.
// Arguments:
//   v1 = First vector.
//   v2 = Second vector.
// NOTE: constrain() corrects crazy FP rounding errors that exceed acos()'s domain.
function vector_angle(v1,v2) = acos(constrain((v1*v2)/(norm(v1)*norm(v2)), -1, 1));


// Function: vector_axis()
// Usage:
//   vector_xis(v1,v2);
// Description:
//   Returns the vector perpendicular to both of the given vectors.
// Arguments:
//   v1 = First vector.
//   v2 = Second vector.
function vector_axis(v1,v2) =
	let(
		eps = 1e-6,
		v1 = point3d(v1/norm(v1)),
		v2 = point3d(v2/norm(v2)),
		v3 = (norm(v1-v2) > eps && norm(v1+v2) > eps)? v2 :
			(norm(vabs(v2)-V_UP) > eps)? V_UP :
			V_RIGHT
	) normalize(cross(v1,v3));


// Section: Coordinates Manipulation

// Function: point2d()
// Description:
//   Returns a 2D vector/point from a 2D or 3D vector.
//   If given a 3D point, removes the Z coordinate.
// Arguments:
//   p = The coordinates to force into a 2D vector/point.
function point2d(p) = [for (i=[0:1]) (p[i]==undef)? 0 : p[i]];


// Function: path2d()
// Description:
//   Returns a list of 2D vectors/points from a list of 2D or 3D vectors/points.
//   If given a 3D point list, removes the Z coordinates from each point.
// Arguments:
//   points = A list of 2D or 3D points/vectors.
function path2d(points) = [for (point = points) point2d(point)];


// Function: point3d()
// Description:
//   Returns a 3D vector/point from a 2D or 3D vector.
// Arguments:
//   p = The coordinates to force into a 3D vector/point.
function point3d(p) = [for (i=[0:2]) (p[i]==undef)? 0 : p[i]];


// Function: path3d()
// Description:
//   Returns a list of 3D vectors/points from a list of 2D or 3D vectors/points.
// Arguments:
//   points = A list of 2D or 3D points/vectors.
function path3d(points) = [for (point = points) point3d(point)];


// Function: translate_points()
// Usage:
//   translate_points(pts, v);
// Description:
//   Moves each point in an array by a given amount.
// Arguments:
//   pts = List of points to translate.
//   v = Amount to translate points by.
function translate_points(pts, v=[0,0,0]) = [for (pt = pts) pt+v];


// Function: scale_points()
// Usage:
//   scale_points(pts, v, [cp]);
// Description:
//   Scales each point in an array by a given amount, around a given centerpoint.
// Arguments:
//   pts = List of points to scale.
//   v = A vector with a scaling factor for each axis.
//   cp = Centerpoint to scale around.
function scale_points(pts, v=[0,0,0], cp=[0,0,0]) = [for (pt = pts) [for (i = [0:len(pt)-1]) (pt[i]-cp[i])*v[i]+cp[i]]];


// Function: rotate_points2d()
// Usage:
//   rotate_points2d(pts, ang, [cp]);
// Description:
//   Rotates each 2D point in an array by a given amount, around an optional centerpoint.
// Arguments:
//   pts = List of 3D points to rotate.
//   ang = Angle to rotate by.
//   cp = 2D Centerpoint to rotate around.  Default: `[0,0]`
function rotate_points2d(pts, ang, cp=[0,0]) = let(
		m = matrix3_zrot(ang)
	) [for (pt = pts) m*point3d(pt-cp)+cp];


// Function: rotate_points3d()
// Usage:
//   rotate_points3d(pts, v, [cp], [reverse]);
//   rotate_points3d(pts, v, axis, [cp], [reverse]);
//   rotate_points3d(pts, from, to, v, [cp], [reverse]);
// Description:
//   Rotates each 3D point in an array by a given amount, around a given centerpoint.
// Arguments:
//   pts = List of points to rotate.
//   v = Rotation angle(s) in degrees.
//   axis = If given, axis vector to rotate around.
//   cp = Centerpoint to rotate around.
//   from = If given, the vector to rotate something from.  Used with `to`.
//   to = If given, the vector to rotate something to.  Used with `from`.
//   reverse = If true, performs an exactly reversed rotation.
function rotate_points3d(pts, v=0, cp=[0,0,0], axis=undef, from=undef, to=undef, reverse=false) =
	let(
		dummy = assertion(is_def(from)==is_def(to), "`from` and `to` must be given together."),
		mrot = reverse? (
			is_def(from)? (
				let (
					from = from / norm(from),
					to = to / norm(from),
					ang = vector_angle(from, to),
					axis = vector_axis(from, to)
				)
				matrix4_rot_by_axis(from, -v) * matrix4_rot_by_axis(axis, -ang)
			) : is_def(axis)? (
				matrix4_rot_by_axis(axis, -v)
			) : is_scalar(v)? (
				matrix4_zrot(-v)
			) : (
				matrix4_xrot(-v.x) * matrix4_yrot(-v.y) * matrix4_zrot(-v.z)
			)
		) : (
			is_def(from)? (
				let (
					from = from / norm(from),
					to = to / norm(from),
					ang = vector_angle(from, to),
					axis = vector_axis(from, to)
				)
				matrix4_rot_by_axis(axis, ang) * matrix4_rot_by_axis(from, v)
			) : is_def(axis)? (
				matrix4_rot_by_axis(axis, v)
			) : is_scalar(v)? (
				matrix4_zrot(v)
			) : (
				matrix4_zrot(v.z) * matrix4_yrot(v.y) * matrix4_xrot(v.x)
			)
		),
		m = matrix4_translate(cp) * mrot * matrix4_translate(-cp)
	) [for (pt = pts) point3d(m*concat(point3d(pt),[1]))];



// Function: rotate_points3d_around_axis()
// Status: DEPRECATED, use `rotate_points3d(pts, v=ang, axis=u, cp=cp)` instead.
// Usage:
//   rotate_points3d_around_axis(pts, ang, u, [cp])
// Description:
//   Rotates each 3D point in an array by a given amount, around a given centerpoint and axis.
// Arguments:
//   pts = List of 3D points to rotate.
//   ang = Angle to rotate by.
//   u = Vector of the axis to rotate around.
//   cp = 3D Centerpoint to rotate around.
function rotate_points3d_around_axis(pts, ang, u=[0,0,0], cp=[0,0,0]) = let(
		m = matrix4_rot_by_axis(u, ang)
	) [for (pt = pts) m*concat(point3d(pt)-cp, 0)+cp];


// Section: Coordinate Systems

// Function: polar_to_xy()
// Usage:
//   polar_to_xy(r, theta);
//   polar_to_xy([r, theta]);
// Description:
//   Convert polar coordinates to 2D cartesian coordinates.
//   Returns [X,Y] cartesian coordinates.
// Arguments:
//   r = distance from the origin.
//   theta = angle in degrees, counter-clockwise of X+.
// Examples:
//   xy = polar_to_xy(20,30);
//   xy = polar_to_xy([40,60]);
function polar_to_xy(r,theta=undef) = let(
		rad = theta==undef? r[0] : r,
		t = theta==undef? r[1] : theta
	) rad*[cos(t), sin(t)];


// Function: xy_to_polar()
// Usage:
//   xy_to_polar(x,y);
//   xy_to_polar([X,Y]);
// Description:
//   Convert 2D cartesian coordinates to polar coordinates.
//   Returns [radius, theta] where theta is the angle counter-clockwise of X+.
// Arguments:
//   x = X coordinate.
//   y = Y coordinate.
// Examples:
//   plr = xy_to_polar(20,30);
//   plr = xy_to_polar([40,60]);
function xy_to_polar(x,y=undef) = let(
		xx = y==undef? x[0] : x,
		yy = y==undef? x[1] : y
	) [norm([xx,yy]), atan2(yy,xx)];


// Function: xyz_to_planar()
// Usage:
//   xyz_to_planar(point, a, b, c);
// Description:
//   Given three points defining a plane, returns the projected planar
//   [X,Y] coordinates of the closest point to a 3D `point`.  The origin
//   of the planar coordinate system [0,0] will be at point `a`, and the
//   Y+ axis direction will be towards point `b`.  This coordinate system
//   can be useful in taking a set of nearly coplanar points, and converting
//   them to a pure XY set of coordinates for manipulation, before convering
//   them back to the original 3D plane.
function xyz_to_planar(point, a, b, c) = let(
	u = normalize(b-a),
	v = normalize(c-a),
	n = normalize(cross(u,v)),
	w = normalize(cross(n,u)),
	relpoint = point-a
) [relpoint * w, relpoint * u];


// Function: planar_to_xyz()
// Usage:
//   planar_to_xyz(point, a, b, c);
// Description:
//   Given three points defining a plane, converts a planar [X,Y]
//   coordinate to the actual corresponding 3D point on the plane.
//   The origin of the planar coordinate system [0,0] will be at point
//   `a`, and the Y+ axis direction will be towards point `b`.
function planar_to_xyz(point, a, b, c) = let(
	u = normalize(b-a),
	v = normalize(c-a),
	n = normalize(cross(u,v)),
	w = normalize(cross(n,u))
) a + point.x * w + point.y * u;


// Function: cylindrical_to_xyz()
// Usage:
//   cylindrical_to_xyz(r, theta, z)
//   cylindrical_to_xyz([r, theta, z])
// Description:
//   Convert cylindrical coordinates to 3D cartesian coordinates.  Returns [X,Y,Z] cartesian coordinates.
// Arguments:
//   r = distance from the Z axis.
//   theta = angle in degrees, counter-clockwise of X+ on the XY plane.
//   z = Height above XY plane.
// Examples:
//   xyz = cylindrical_to_xyz(20,30,40);
//   xyz = cylindrical_to_xyz([40,60,50]);
function cylindrical_to_xyz(r,theta=undef,z=undef) = let(
		rad = theta==undef? r[0] : r,
		t = theta==undef? r[1] : theta,
		zed = theta==undef? r[2] : z
	) [rad*cos(t), rad*sin(t), zed];


// Function: xyz_to_cylindrical()
// Usage:
//   xyz_to_cylindrical(x,y,z)
//   xyz_to_cylindrical([X,Y,Z])
// Description:
//   Convert 3D cartesian coordinates to cylindrical coordinates.
//   Returns [radius,theta,Z]. Theta is the angle counter-clockwise
//   of X+ on the XY plane.  Z is height above the XY plane.
// Arguments:
//   x = X coordinate.
//   y = Y coordinate.
//   z = Z coordinate.
// Examples:
//   cyl = xyz_to_cylindrical(20,30,40);
//   cyl = xyz_to_cylindrical([40,50,70]);
function xyz_to_cylindrical(x,y=undef,z=undef) = let(
		p = is_scalar(x)? [x, default(y,0), default(z,0)] : point3d(x)
	) [norm([p.x,p.y]), atan2(p.y,p.x), p.z];


// Function: spherical_to_xyz()
// Usage:
//   spherical_to_xyz(r, theta, phi);
//   spherical_to_xyz([r, theta, phi]);
// Description:
//   Convert spherical coordinates to 3D cartesian coordinates.
//   Returns [X,Y,Z] cartesian coordinates.
// Arguments:
//   r = distance from origin.
//   theta = angle in degrees, counter-clockwise of X+ on the XY plane.
//   phi = angle in degrees from the vertical Z+ axis.
// Examples:
//   xyz = spherical_to_xyz(20,30,40);
//   xyz = spherical_to_xyz([40,60,50]);
function spherical_to_xyz(r,theta=undef,phi=undef) = let(
		rad = theta==undef? r[0] : r,
		t = theta==undef? r[1] : theta,
		p = theta==undef? r[2] : phi
	) rad*[sin(p)*cos(t), sin(p)*sin(t), cos(p)];


// Function: xyz_to_spherical()
// Usage:
//   xyz_to_spherical(x,y,z)
//   xyz_to_spherical([X,Y,Z])
// Description:
//   Convert 3D cartesian coordinates to spherical coordinates.
//   Returns [r,theta,phi], where phi is the angle from the Z+ pole,
//   and theta is degrees counter-clockwise of X+ on the XY plane.
// Arguments:
//   x = X coordinate.
//   y = Y coordinate.
//   z = Z coordinate.
// Examples:
//   sph = xyz_to_spherical(20,30,40);
//   sph = xyz_to_spherical([40,50,70]);
function xyz_to_spherical(x,y=undef,z=undef) = let(
		p = is_scalar(x)? [x, default(y,0), default(z,0)] : point3d(x)
	) [norm(p), atan2(p.y,p.x), atan2(norm([p.x,p.y]),p.z)];


// Function: altaz_to_xyz()
// Usage:
//   altaz_to_xyz(alt, az, r);
//   altaz_to_xyz([alt, az, r]);
// Description:
//   Convert altitude/azimuth/range coordinates to 3D cartesian coordinates.
//   Returns [X,Y,Z] cartesian coordinates.
// Arguments:
//   alt = altitude angle in degrees above the XY plane.
//   az = azimuth angle in degrees clockwise of Y+ on the XY plane.
//   r = distance from origin.
// Examples:
//   xyz = altaz_to_xyz(20,30,40);
//   xyz = altaz_to_xyz([40,60,50]);
function altaz_to_xyz(alt,az=undef,r=undef) = let(
		p = az==undef? alt[0] : alt,
		t = 90 - (az==undef? alt[1] : az),
		rad = az==undef? alt[2] : r
	) rad*[cos(p)*cos(t), cos(p)*sin(t), sin(p)];


// Function: xyz_to_altaz()
// Usage:
//   xyz_to_altaz(x,y,z);
//   xyz_to_altaz([X,Y,Z]);
// Description:
//   Convert 3D cartesian coordinates to altitude/azimuth/range coordinates.
//   Returns [altitude,azimuth,range], where altitude is angle above the
//   XY plane, azimuth is degrees clockwise of Y+ on the XY plane, and
//   range is the distance from the origin.
// Arguments:
//   x = X coordinate.
//   y = Y coordinate.
//   z = Z coordinate.
// Examples:
//   aa = xyz_to_altaz(20,30,40);
//   aa = xyz_to_altaz([40,50,70]);
function xyz_to_altaz(x,y=undef,z=undef) = let(
		p = is_scalar(x)? [x, default(y,0), default(z,0)] : point3d(x)
	) [atan2(p.z,norm([p.x,p.y])), atan2(p.x,p.y), norm(p)];


// Section: Matrix Manipulation

// Function: ident()
// Description: Create an `n` by `n` identity matrix.
// Arguments:
//   n = The size of the identity matrix square, `n` by `n`.
function ident(n) = [for (i = [0:n-1]) [for (j = [0:n-1]) (i==j)?1:0]];



// Function: matrix_transpose()
// Description: Returns the transposition of the given matrix.
// Example:
//   m = [
//       [11,12,13,14],
//       [21,22,23,24],
//       [31,32,33,34],
//       [41,42,43,44]
//   ];
//   tm = matrix_transpose(m);
//   // Returns:
//   // [
//   //     [11,21,31,41],
//   //     [12,22,32,42],
//   //     [13,23,33,43],
//   //     [14,24,34,44]
//   // ]
function matrix_transpose(m) = [for (i=[0:len(m[0])-1]) [for (j=[0:len(m)-1]) m[j][i]]];



// Function: mat3_to_mat4()
// Description: Takes a 3x3 matrix and returns its 4x4 equivalent.
function mat3_to_mat4(m) = concat(
	[for (r = [0:2])
		concat(
			[for (c = [0:2]) m[r][c]],
			[0]
		)
	],
	[[0, 0, 0, 1]]
);



// Function: matrix3_translate()
// Description:
//   Returns the 3x3 matrix to perform a 2D translation.
// Arguments:
//   v = 2D Offset to translate by.  [X,Y]
function matrix3_translate(v) = [
	[1, 0, v.x],
	[0, 1, v.y],
	[0 ,0,   1]
];


// Function: matrix4_translate()
// Description:
//   Returns the 4x4 matrix to perform a 3D translation.
// Arguments:
//   v = 3D offset to translate by.  [X,Y,Z]
function matrix4_translate(v) = [
	[1, 0, 0, v.x],
	[0, 1, 0, v.y],
	[0, 0, 1, v.z],
	[0 ,0, 0,   1]
];


// Function: matrix3_scale()
// Description:
//   Returns the 3x3 matrix to perform a 2D scaling transformation.
// Arguments:
//   v = 2D vector of scaling factors.  [X,Y]
function matrix3_scale(v) = [
	[v.x,   0, 0],
	[  0, v.y, 0],
	[  0,   0, 1]
];


// Function: matrix4_scale()
// Description:
//   Returns the 4x4 matrix to perform a 3D scaling transformation.
// Arguments:
//   v = 3D vector of scaling factors.  [X,Y,Z]
function matrix4_scale(v) = [
	[v.x,   0,   0, 0],
	[  0, v.y,   0, 0],
	[  0,   0, v.z, 0],
	[  0,   0,   0, 1]
];


// Function: matrix3_zrot()
// Description:
//   Returns the 3x3 matrix to perform a rotation of a 2D vector around the Z axis.
// Arguments:
//   ang = Number of degrees to rotate.
function matrix3_zrot(ang) = [
	[cos(ang), -sin(ang), 0],
	[sin(ang),  cos(ang), 0],
	[       0,         0, 1]
];


// Function: matrix4_xrot()
// Description:
//   Returns the 4x4 matrix to perform a rotation of a 3D vector around the X axis.
// Arguments:
//   ang = number of degrees to rotate.
function matrix4_xrot(ang) = [
	[1,        0,         0,   0],
	[0, cos(ang), -sin(ang),   0],
	[0, sin(ang),  cos(ang),   0],
	[0,        0,         0,   1]
];


// Function: matrix4_yrot()
// Description:
//   Returns the 4x4 matrix to perform a rotation of a 3D vector around the Y axis.
// Arguments:
//   ang = Number of degrees to rotate.
function matrix4_yrot(ang) = [
	[ cos(ang), 0, sin(ang),   0],
	[        0, 1,        0,   0],
	[-sin(ang), 0, cos(ang),   0],
	[        0, 0,        0,   1]
];


// Function: matrix4_zrot()
// Usage:
//   matrix4_zrot(ang)
// Description:
//   Returns the 4x4 matrix to perform a rotation of a 3D vector around the Z axis.
// Arguments:
//   ang = number of degrees to rotate.
function matrix4_zrot(ang) = [
	[cos(ang), -sin(ang), 0, 0],
	[sin(ang),  cos(ang), 0, 0],
	[       0,         0, 1, 0],
	[       0,         0, 0, 1]
];


// Function: matrix4_rot_by_axis()
// Usage:
//   matrix4_rot_by_axis(u, ang);
// Description:
//   Returns the 4x4 matrix to perform a rotation of a 3D vector around an axis.
// Arguments:
//   u = 3D axis vector to rotate around.
//   ang = number of degrees to rotate.
function matrix4_rot_by_axis(u, ang) = let(
	u = normalize(u),
	c = cos(ang),
	c2 = 1-c,
	s = sin(ang)
) [
	[u[0]*u[0]*c2+c     , u[0]*u[1]*c2-u[2]*s, u[0]*u[2]*c2+u[1]*s, 0],
	[u[1]*u[0]*c2+u[2]*s, u[1]*u[1]*c2+c     , u[1]*u[2]*c2-u[0]*s, 0],
	[u[2]*u[0]*c2-u[1]*s, u[2]*u[1]*c2+u[0]*s, u[2]*u[2]*c2+c     , 0],
	[                  0,                   0,                   0, 1]
];


// Function: matrix3_skew()
// Usage:
//   matrix3_skew(xa, ya)
// Description:
//   Returns the 3x3 matrix to skew a 2D vector along the XY plane.
// Arguments:
//   xa = Skew angle, in degrees, in the direction of the X axis.
//   ya = Skew angle, in degrees, in the direction of the Y axis.
function matrix3_skew(xa, ya) = [
	[1,       tan(xa), 0],
	[tan(ya), 1,       0],
	[0,       0,       1]
];



// Function: matrix4_skew_xy()
// Usage:
//   matrix4_skew_xy(xa, ya)
// Description:
//   Returns the 4x4 matrix to perform a skew transformation along the XY plane..
// Arguments:
//   xa = Skew angle, in degrees, in the direction of the X axis.
//   ya = Skew angle, in degrees, in the direction of the Y axis.
function matrix4_skew_xy(xa, ya) = [
	[1, 0, tan(xa), 0],
	[0, 1, tan(ya), 0],
	[0, 0,       1, 0],
	[0, 0,       0, 1]
];



// Function: matrix4_skew_xz()
// Usage:
//   matrix4_skew_xz(xa, za)
// Description:
//   Returns the 4x4 matrix to perform a skew transformation along the XZ plane.
// Arguments:
//   xa = Skew angle, in degrees, in the direction of the X axis.
//   za = Skew angle, in degrees, in the direction of the Z axis.
function matrix4_skew_xz(xa, za) = [
	[1, tan(xa), 0, 0],
	[0,       1, 0, 0],
	[0, tan(za), 1, 0],
	[0,       0, 0, 1]
];


// Function: matrix4_skew_yz()
// Usage:
//   matrix4_skew_yz(ya, za)
// Description:
//   Returns the 4x4 matrix to perform a skew transformation along the YZ plane.
// Arguments:
//   ya = Skew angle, in degrees, in the direction of the Y axis.
//   za = Skew angle, in degrees, in the direction of the Z axis.
function matrix4_skew_yz(ya, za) = [
	[      1, 0, 0, 0],
	[tan(ya), 1, 0, 0],
	[tan(za), 0, 1, 0],
	[      0, 0, 0, 1]
];


// Function: matrix3_mult()
// Usage:
//   matrix3_mult(matrices)
// Description:
//   Returns a 3x3 transformation matrix which results from applying each matrix in `matrices` in order.
// Arguments:
//   matrices = A list of 3x3 matrices.
//   m = Optional starting matrix to apply everything to.
function matrix3_mult(matrices, m=ident(3), i=0) =
	(i>=len(matrices))? m :
	let (newmat = is_def(m)? matrices[i] * m : matrices[i])
		matrix3_mult(matrices, m=newmat, i=i+1);


// Function: matrix4_mult()
// Usage:
//   matrix4_mult(matrices)
// Description:
//   Returns a 4x4 transformation matrix which results from applying each matrix in `matrices` in order.
// Arguments:
//   matrices = A list of 4x4 matrices.
//   m = Optional starting matrix to apply everything to.
function matrix4_mult(matrices, m=ident(4), i=0) =
	(i>=len(matrices))? m :
	let (newmat = is_def(m)? matrices[i] * m : matrices[i])
		matrix4_mult(matrices, m=newmat, i=i+1);


// Function: matrix3_apply()
// Usage:
//   matrix3_apply(pts, matrices)
// Description:
//   Given a list of transformation matrices, applies them in order to the points in the point list.
// Arguments:
//   pts = A list of 2D points to transform.
//   matrices = A list of 3x3 matrices to apply, in order.
// Example:
//   npts = matrix3_apply(
//       pts = [for (x=[0:3]) [5*x,0]],
//       matrices =[
//           matrix3_scale([3,1]),
//           matrix3_rot(90),
//           matrix3_translate([5,5])
//       ]
//   );  // Returns [[5,5], [5,20], [5,35], [5,50]]
function matrix3_apply(pts, matrices) = let(m = matrix3_mult(matrices)) [for (p = pts) point2d(m * concat(point2d(p),[1]))];


// Function: matrix4_apply()
// Usage:
//   matrix4_apply(pts, matrices)
// Description:
//   Given a list of transformation matrices, applies them in order to the points in the point list.
// Arguments:
//   pts = A list of 3D points to transform.
//   matrices = A list of 4x4 matrices to apply, in order.
// Example:
//   npts = matrix4_apply(
//     pts = [for (x=[0:3]) [5*x,0,0]],
//     matrices =[
//       matrix4_scale([2,1,1]),
//       matrix4_zrot(90),
//       matrix4_translate([5,5,10])
//     ]
//   );  // Returns [[5,5,10], [5,15,10], [5,25,10], [5,35,10]]

function matrix4_apply(pts, matrices) = let(m = matrix4_mult(matrices)) [for (p = pts) point3d(m * concat(point3d(p),[1]))];


// Section: Geometry

// Function: point_on_segment()
// Usage:
//   point_on_segment(point, edge);
// Description:
//   Determine if the point is on the line segment between two points.
//   Returns true if yes, and false if not.  
// Arguments:
//   point = The point to check colinearity of.
//   edge = Array of two points forming the line segment to test against.
function point_on_segment(point, edge) =
	point==edge[0] || point==edge[1] ||  // The point is an endpoint
	sign(edge[0].x-point.x)==sign(point.x-edge[1].x)  // point is in between the
		&& sign(edge[0].y-point.y)==sign(point.y-edge[1].y)  // edge endpoints 
		&& point_left_of_segment(point, edge)==0;  // and on the line defined by edge


// Function: point_left_of_segment()
// Usage:
//   point_left_of_segment(point, edge);
// Description:
//   Return >0 if point is left of the line defined by edge.
//   Return =0 if point is on the line.
//   Return <0 if point is right of the line.
// Arguments:
//   point = The point to check position of.
//   edge = Array of two points forming the line segment to test against.
function point_left_of_segment(point, edge) =
	(edge[1].x-edge[0].x) * (point.y-edge[0].y) - (point.x-edge[0].x) * (edge[1].y-edge[0].y);
  

// Internal non-exposed function.
function _point_above_below_segment(point, edge) =
	edge[0].y <= point.y? (
		(edge[1].y > point.y && point_left_of_segment(point, edge) > 0)? 1 : 0
	) : (
		(edge[1].y <= point.y && point_left_of_segment(point, edge) < 0)? -1 : 0
	);


// Function: point_in_polygon()
// Usage:
//   point_in_polygon(point, path)
// Description:
//   This function tests whether the given point is inside, outside or on the boundary of
//   the specified polygon using the Winding Number method.  (http://geomalgorithms.com/a03-_inclusion.html)
//   The polygon is given as a list of points, not including the repeated end point.
//   Returns -1 if the point is outside the polyon.
//   Returns 0 if the point is on the boundary.
//   Returns 1 if the point lies in the interior.
//   The polygon does not need to be simple: it can have self-intersections.
//   But the polygon cannot have holes (it must be simply connected).
//   Rounding error may give mixed results for points on or near the boundary.
// Arguments:
//   point = The point to check position of.
//   path = The list of 2D path points forming the perimeter of the polygon.
function point_in_polygon(point, path) =
	// Does the point lie on any edges?  If so return 0. 
	sum([for(i=[0:len(path)-1]) point_on_segment(point, select(path, i, i+1))?1:0])>0 ? 0 : 
	// Otherwise compute winding number and return 1 for interior, -1 for exterior
	sum([for(i=[0:len(path)-1]) _point_above_below_segment(point, select(path, i, i+1))]) != 0 ? 1 : -1;


// Function: pointlist_bounds()
// Usage:
//   pointlist_bounds(pts);
// Description:
//   Finds the bounds containing all the points in pts.
//   Returns [[minx, miny, minz], [maxx, maxy, maxz]]
// Arguments:
//   pts = List of points.
function pointlist_bounds(pts) = [
	[for (a=[0:2]) min([ for (x=pts) point3d(x)[a] ]) ],
	[for (a=[0:2]) max([ for (x=pts) point3d(x)[a] ]) ]
];


// Function: triangle_area2d()
// Usage:
//   triangle_area2d(a,b,c);
// Description:
//   Returns the area of a triangle formed between three vertices.
//   Result will be negative if the points are in clockwise order.
// Examples:
//   triangle_area2d([0,0], [5,10], [10,0]);  // Returns -50
//   triangle_area2d([10,0], [5,10], [0,0]);  // Returns 50
function triangle_area2d(a,b,c) =
	(
		a.x * (b.y - c.y) + 
		b.x * (c.y - a.y) + 
		c.x * (a.y - b.y)
	) / 2;


// Function: right_of_line2d()
// Usage:
//   right_of_line2d(line, pt)
// Description:
//   Returns true if the given point is to the left of the given line.
// Arguments:
//   line = A list of two points.
//   pt = The point to test.
function right_of_line2d(line, pt) =
	triangle_area2d(line[0], line[1], pt) < 0;


// Function: collinear()
// Usage:
//   collinear(a, b, c, [eps]);
// Description:
//   Returns true if three points are co-linear.
// Arguments:
//   a = First point.
//   b = Second point.
//   c = Third point.
//   eps = Acceptable max angle variance.  Default: EPSILON (1e-9) degrees.
function collinear(a, b, c, eps=EPSILON) =
	abs(vector_angle(b-a,c-a)) < eps;


// Function: collinear_indexed()
// Usage:
//   collinear_indexed(points, a, b, c, [eps]);
// Description:
//   Returns true if three points are co-linear.
// Arguments:
//   points = A list of points.
//   a = Index in `points` of first point.
//   b = Index in `points` of second point.
//   c = Index in `points` of third point.
//   eps = Acceptable max angle variance.  Default: EPSILON (1e-9) degrees.
function collinear_indexed(points, a, b, c, eps=EPSILON) =
	let(
		p1=points[a],
		p2=points[b],
		p3=points[c]
	) abs(vector_angle(p2-p1,p3-p1)) < eps;


// Function: plane3pt()
// Usage:
//   plane3pt(p1, p2, p3);
// Description:
//   Generates the cartesian equation of a plane from three non-colinear points on the plane.
//   Returns [A,B,C,D] where Ax+By+Cz+D=0 is the equation of a plane.
// Arguments:
//   p1 = The first point on the plane.
//   p2 = The second point on the plane.
//   p3 = The third point on the plane.
function plane3pt(p1, p2, p3) =
	let(normal = normalize(cross(p3-p1, p2-p1))) concat(normal, [normal*p1]);


// Function: plane3pt_indexed()
// Usage:
//   plane3pt_indexed(points, i1, i2, i3);
// Description:
//   Given a list of points, and the indexes of three of those points,
//   generates the cartesian equation of a plane that those points all
//   lie on.  Requires that the three indexed points be non-collinear.
//   Returns [A,B,C,D] where Ax+By+Cz+D=0 is the equation of a plane.
// Arguments:
//   points = A list of points.
//   i1 = The index into `points` of the first point on the plane.
//   i2 = The index into `points` of the second point on the plane.
//   i3 = The index into `points` of the third point on the plane.
function plane3pt_indexed(points, i1, i2, i3) =
	let(
		p1 = points[i1],
		p2 = points[i2],
		p3 = points[i3],
		normal = normalize(cross(p3-p1, p2-p1))
	) concat(normal, [normal*p1]);


// Function: distance_from_plane()
// Usage:
//   distance_from_plane(plane, point)
// Description:
//   Given a plane as [A,B,C,D] where the cartesian equation for that plane
//   is Ax+By+Cz+D=0, determines how far from that plane the given point is.
//   The returned distance will be positive if the point is in front of the
//   plane; on the same side of the plane as the normal of that plane points
//   towards.  If the point is behind the plane, then the distance returned
//   will be negative.  The normal of the plane is the same as [A,B,C].
// Arguments:
//   plane = The [A,B,C,D] values for the equation of the plane.
//   point = The point to test.
function distance_from_plane(plane, point) =
	[plane.x, plane.y, plane.z] * point - plane[3];


// Function: coplanar()
// Usage:
//   coplanar(plane, point);
// Description:
//   Given a plane as [A,B,C,D] where the cartesian equation for that plane
//   is Ax+By+Cz+D=0, determines if the given point is on that plane.
//   Returns true if the point is on that plane.
// Arguments:
//   plane = The [A,B,C,D] values for the equation of the plane.
//   point = The point to test.
function coplanar(plane, point) =
	abs(distance_from_plane(plane, point)) <= EPSILON;


// Function: in_front_of_plane()
// Usage:
//   in_front_of_plane(plane, point);
// Description:
//   Given a plane as [A,B,C,D] where the cartesian equation for that plane
//   is Ax+By+Cz+D=0, determines if the given point is on the side of that
//   plane that the normal points towards.  The normal of the plane is the
//   same as [A,B,C].
// Arguments:
//   plane = The [A,B,C,D] values for the equation of the plane.
//   point = The point to test.
function in_front_of_plane(plane, point) =
	distance_from_plane(plane, point) > EPSILON;


// Function: simplify_path()
// Description:
//   Takes a path and removes unnecessary collinear points.
// Usage:
//   simplify_path(path, [eps])
// Arguments:
//   path = A list of 2D path points.
//   eps = Largest angle variance allowed.  Default: EPSILON (1-e9) degrees.
function simplify_path(path, eps=EPSILON, _a=0, _b=2, _acc=[]) =
	(_b >= len(path))? concat([path[0]], _acc, [path[len(path)-1]]) :
	simplify_path(
		path, eps,
		(collinear_indexed(path, _a, _b-1, _b, eps=eps)? _a : _b-1),
		_b+1,
		(collinear_indexed(path, _a, _b-1, _b, eps=eps)? _acc : concat(_acc, [path[_b-1]]))
	);


// Function: simplify_path_indexed()
// Description:
//   Takes a list of points, and a path as a list of indexes into `points`,
//   and removes all path points that are unecessarily collinear.
// Usage:
//   simplify_path_indexed(path, eps)
// Arguments:
//   points = A list of points.
//   path = A list of indexes into `points` that forms a path.
//   eps = Largest angle variance allowed.  Default: EPSILON (1-e9) degrees.
function simplify_path_indexed(points, path, eps=EPSILON, _a=0, _b=2, _acc=[]) =
	(_b >= len(path))? concat([path[0]], _acc, [path[len(path)-1]]) :
	simplify_path_indexed(
		points, path, eps,
		(collinear_indexed(points, path[_a], path[_b-1], path[_b], eps=eps)? _a : _b-1),
		_b+1,
		(collinear_indexed(points, path[_a], path[_b-1], path[_b], eps=eps)? _acc : concat(_acc, [path[_b-1]]))
	);


// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
