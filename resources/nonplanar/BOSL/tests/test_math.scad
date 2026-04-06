include <BOSL/constants.scad>
include <BOSL/math.scad>

eps = 1e-9;

// Simple Calculations

module test_quant() {
	assert(quant(-4,3) == -3);
	assert(quant(-3,3) == -3);
	assert(quant(-2,3) == -3);
	assert(quant(-1,3) == 0);
	assert(quant(0,3) == 0);
	assert(quant(1,3) == 0);
	assert(quant(2,3) == 3);
	assert(quant(3,3) == 3);
	assert(quant(4,3) == 3);
	assert(quant(7,3) == 6);
}
test_quant();


module test_quantdn() {
	assert(quantdn(-4,3) == -6);
	assert(quantdn(-3,3) == -3);
	assert(quantdn(-2,3) == -3);
	assert(quantdn(-1,3) == -3);
	assert(quantdn(0,3) == 0);
	assert(quantdn(1,3) == 0);
	assert(quantdn(2,3) == 0);
	assert(quantdn(3,3) == 3);
	assert(quantdn(4,3) == 3);
	assert(quantdn(7,3) == 6);
}
test_quantdn();


module test_quantup() {
	assert(quantup(-4,3) == -3);
	assert(quantup(-3,3) == -3);
	assert(quantup(-2,3) == 0);
	assert(quantup(-1,3) == 0);
	assert(quantup(0,3) == 0);
	assert(quantup(1,3) == 3);
	assert(quantup(2,3) == 3);
	assert(quantup(3,3) == 3);
	assert(quantup(4,3) == 6);
	assert(quantup(7,3) == 9);
}
test_quantup();


module test_constrain() {
	assert(constrain(-2,-1,1) == -1);
	assert(constrain(-1.75,-1,1) == -1);
	assert(constrain(-1,-1,1) == -1);
	assert(constrain(-0.75,-1,1) == -0.75);
	assert(constrain(0,-1,1) == 0);
	assert(constrain(0.75,-1,1) == 0.75);
	assert(constrain(1,-1,1) == 1);
	assert(constrain(1.75,-1,1) == 1);
	assert(constrain(2,-1,1) == 1);
}
test_constrain();


module test_posmod() {
	assert(posmod(-5,3) == 1);
	assert(posmod(-4,3) == 2);
	assert(posmod(-3,3) == 0);
	assert(posmod(-2,3) == 1);
	assert(posmod(-1,3) == 2);
	assert(posmod(0,3) == 0);
	assert(posmod(1,3) == 1);
	assert(posmod(2,3) == 2);
	assert(posmod(3,3) == 0);
}
test_posmod();


module test_modrange() {
	assert(modrange(-5,5,3) == [1,2]);
	assert(modrange(-1,4,3) == [2,0,1]);
	assert(modrange(1,8,10,step=2) == [1,3,5,7]);
	assert(modrange(5,12,10,step=2) == [5,7,9,1]);
}
test_modrange();


module test_segs() {
	assert(segs(50,$fn=8) == 8);
	assert(segs(50,$fa=2,$fs=2) == 158);
}
test_segs();


module test_lerp() {
	assert(lerp(-20,20,0) == -20);
	assert(lerp(-20,20,0.25) == -10);
	assert(lerp(-20,20,0.5) == 0);
	assert(lerp(-20,20,0.75) == 10);
	assert(lerp(-20,20,1) == 20);
	assert(lerp([10,10],[30,-10],0.5) == [20,0]);
}
test_lerp();


module test_hypot() {
	assert(hypot(20,30) == norm([20,30]));
}
test_hypot();


module test_sinh() {
	assert(abs(sinh(-2)+3.6268604078) < eps);
	assert(abs(sinh(-1)+1.1752011936) < eps);
	assert(abs(sinh(0)) < eps);
	assert(abs(sinh(1)-1.1752011936) < eps);
	assert(abs(sinh(2)-3.6268604078) < eps);
}
test_sinh();


module test_cosh() {
	assert(abs(cosh(-2)-3.7621956911) < eps);
	assert(abs(cosh(-1)-1.5430806348) < eps);
	assert(abs(cosh(0)-1) < eps);
	assert(abs(cosh(1)-1.5430806348) < eps);
	assert(abs(cosh(2)-3.7621956911) < eps);
}
test_cosh();


module test_tanh() {
	assert(abs(tanh(-2)+0.9640275801) < eps);
	assert(abs(tanh(-1)+0.761594156) < eps);
	assert(abs(tanh(0)) < eps);
	assert(abs(tanh(1)-0.761594156) < eps);
	assert(abs(tanh(2)-0.9640275801) < eps);
}
test_tanh();


module test_asinh() {
	assert(abs(asinh(sinh(-2))+2) < eps);
	assert(abs(asinh(sinh(-1))+1) < eps);
	assert(abs(asinh(sinh(0))) < eps);
	assert(abs(asinh(sinh(1))-1) < eps);
	assert(abs(asinh(sinh(2))-2) < eps);
}
test_asinh();


module test_acosh() {
	assert(abs(acosh(cosh(-2))-2) < eps);
	assert(abs(acosh(cosh(-1))-1) < eps);
	assert(abs(acosh(cosh(0))) < eps);
	assert(abs(acosh(cosh(1))-1) < eps);
	assert(abs(acosh(cosh(2))-2) < eps);
}
test_acosh();


module test_atanh() {
	assert(abs(atanh(tanh(-2))+2) < eps);
	assert(abs(atanh(tanh(-1))+1) < eps);
	assert(abs(atanh(tanh(0))) < eps);
	assert(abs(atanh(tanh(1))-1) < eps);
	assert(abs(atanh(tanh(2))-2) < eps);
}
test_atanh();


module test_sum() {
	assert(sum([1,2,3]) == 6);
	assert(sum([-2,-1,0,1,2]) == 0);
	assert(sum([[1,2,3], [3,4,5], [5,6,7]]) == [9,12,15]);
}
test_sum();


module test_sum_of_squares() {
	assert(sum_of_squares([1,2,3]) == 14);
	assert(sum_of_squares([1,2,4]) == 21);
	assert(sum_of_squares([-3,-2,-1]) == 14);
}
test_sum_of_squares();


module test_sum_of_sines() {
	assert(sum_of_sines(0, [[3,4,0],[2,2,0]]) == 0);
	assert(sum_of_sines(45, [[3,4,0],[2,2,0]]) == 2);
	assert(sum_of_sines(90, [[3,4,0],[2,2,0]]) == 0);
	assert(sum_of_sines(135, [[3,4,0],[2,2,0]]) == -2);
	assert(sum_of_sines(180, [[3,4,0],[2,2,0]]) == 0);
}
test_sum_of_sines();


module test_mean() {
	assert(mean([2,3,4]) == 3);
	assert(mean([[1,2,3], [3,4,5], [5,6,7]]) == [3,4,5]);
}
test_mean();


// Logic


module test_compare_vals() {
	assert(compare_vals(-10,0) == -1);
	assert(compare_vals(10,0) == 1);
	assert(compare_vals(10,10) == 0);

	assert(compare_vals("abc","abcd") == -1);
	assert(compare_vals("abcd","abc") == 1);
	assert(compare_vals("abcd","abcd") == 0);

	assert(compare_vals(false,false) == 0);
	assert(compare_vals(true,false) == 1);
	assert(compare_vals(false,true) == -1);
	assert(compare_vals(true,true) == 0);

	assert(compare_vals([2,3,4], [2,3,4,5]) == -1);
	assert(compare_vals([2,3,4,5], [2,3,4,5]) == 0);
	assert(compare_vals([2,3,4,5], [2,3,4]) == 1);
	assert(compare_vals([2,3,4,5], [2,3,5,5]) == -1);
	assert(compare_vals([[2,3,4,5]], [[2,3,5,5]]) == -1);

	assert(compare_vals([[2,3,4],[3,4,5]], [[2,3,4], [3,4,5]]) == 0);
	assert(compare_vals([[2,3,4],[3,4,5]], [[2,3,4,5], [3,4,5]]) == -1);
	assert(compare_vals([[2,3,4],[3,4,5]], [[2,3,4], [3,4,5,6]]) == -1);
	assert(compare_vals([[2,3,4,5],[3,4,5]], [[2,3,4], [3,4,5]]) == 1);
	assert(compare_vals([[2,3,4],[3,4,5,6]], [[2,3,4], [3,4,5]]) == 1);
	assert(compare_vals([[2,3,4],[3,5,5]], [[2,3,4], [3,4,5]]) == 1);
	assert(compare_vals([[2,3,4],[3,4,5]], [[2,3,4], [3,5,5]]) == -1);
}
test_compare_vals();


module test_compare_lists() {
	assert(compare_lists([2,3,4], [2,3,4,5]) == -1);
	assert(compare_lists([2,3,4,5], [2,3,4,5]) == 0);
	assert(compare_lists([2,3,4,5], [2,3,4]) == 1);
	assert(compare_lists([2,3,4,5], [2,3,5,5]) == -1);

	assert(compare_lists([[2,3,4],[3,4,5]], [[2,3,4], [3,4,5]]) == 0);
	assert(compare_lists([[2,3,4],[3,4,5]], [[2,3,4,5], [3,4,5]]) == -1);
	assert(compare_lists([[2,3,4],[3,4,5]], [[2,3,4], [3,4,5,6]]) == -1);
	assert(compare_lists([[2,3,4,5],[3,4,5]], [[2,3,4], [3,4,5]]) == 1);
	assert(compare_lists([[2,3,4],[3,4,5,6]], [[2,3,4], [3,4,5]]) == 1);
	assert(compare_lists([[2,3,4],[3,5,5]], [[2,3,4], [3,4,5]]) == 1);
	assert(compare_lists([[2,3,4],[3,4,5]], [[2,3,4], [3,5,5]]) == -1);

	assert(compare_lists("cat", "bat") == 1);
	assert(compare_lists(["cat"], ["bat"]) == 1);
}
test_compare_lists();


module test_any() {
	assert(any([0,false,undef]) == false);
	assert(any([1,false,undef]) == true);
	assert(any([1,5,true]) == true);
	assert(any([[0,0], [0,0]]) == false);
	assert(any([[0,0], [1,0]]) == true);
}
test_any();


module test_all() {
	assert(all([0,false,undef]) == false);
	assert(all([1,false,undef]) == false);
	assert(all([1,5,true]) == true);
	assert(all([[0,0], [0,0]]) == false);
	assert(all([[0,0], [1,0]]) == false);
	assert(all([[1,1], [1,1]]) == true);
}
test_all();


module test_count_true() {
	assert(count_true([0,false,undef]) == 0);
	assert(count_true([1,false,undef]) == 1);
	assert(count_true([1,5,false]) == 2);
	assert(count_true([1,5,true]) == 3);
	assert(count_true([[0,0], [0,0]]) == 0);
	assert(count_true([[0,0], [1,0]]) == 1);
	assert(count_true([[1,1], [1,1]]) == 4);
	assert(count_true([[1,1], [1,1]], nmax=3) == 3);
}
test_count_true();



// List/Array Ops

module test_cdr() {
	assert(cdr([]) == []);
	assert(cdr([88]) == []);
	assert(cdr([1,2,3]) == [2,3]);
	assert(cdr(["a","b","c"]) == ["b","c"]);
}
test_cdr();


module test_replist() {
	assert(replist(1, 4) == [1,1,1,1]);
	assert(replist(8, [2,3]) == [[8,8,8], [8,8,8]]);
	assert(replist(0, [2,2,3]) == [[[0,0,0],[0,0,0]], [[0,0,0],[0,0,0]]]);
	assert(replist([1,2,3],3) == [[1,2,3], [1,2,3], [1,2,3]]);
}
test_replist();


module test_in_list() {
	assert(in_list("bar", ["foo", "bar", "baz"]));
	assert(!in_list("bee", ["foo", "bar", "baz"]));
	assert(in_list("bar", [[2,"foo"], [4,"bar"], [3,"baz"]], idx=1));
}
test_in_list();


module test_slice() {
	assert(slice([3,4,5,6,7,8,9], 3, 5) == [6,7]);
	assert(slice([3,4,5,6,7,8,9], 2, -1) == [5,6,7,8,9]);
	assert(slice([3,4,5,6,7,8,9], 1, 1) == []);
	assert(slice([3,4,5,6,7,8,9], 6, -1) == [9]);
	assert(slice([3,4,5,6,7,8,9], 2, -2) == [5,6,7,8]);
}
test_slice();


module test_select() {
	l = [3,4,5,6,7,8,9];
	assert(select(l, 5, 6) == [8,9]);
	assert(select(l, 5, 8) == [8,9,3,4]);
	assert(select(l, 5, 2) == [8,9,3,4,5]);
	assert(select(l, -3, -1) == [7,8,9]);
	assert(select(l, 3, 3) == [6]);
	assert(select(l, 4) == 7);
	assert(select(l, -2) == 8);
	assert(select(l, [1:3]) == [4,5,6]);
	assert(select(l, [1,3]) == [4,6]);
}
test_select();


module test_reverse() {
	assert(reverse([3,4,5,6]) == [6,5,4,3]);
}
test_reverse();


module test_array_subindex() {
	v = [[1,2,3,4],[5,6,7,8],[9,10,11,12],[13,14,15,16]];
	assert(array_subindex(v,2) == [3, 7, 11, 15]);
	assert(array_subindex(v,[2,1]) == [[3, 2], [7, 6], [11, 10], [15, 14]]);
	assert(array_subindex(v,[1:3]) == [[2, 3, 4], [6, 7, 8], [10, 11, 12], [14, 15, 16]]);
}
test_array_subindex();


module test_list_range() {
	assert(list_range(4) == [0,1,2,3]);
	assert(list_range(n=4, step=2) == [0,2,4,6]);
	assert(list_range(n=4, s=3, step=3) == [3,6,9,12]);
	assert(list_range(n=4, s=3, e=9, step=3) == [3,6,9]);
	assert(list_range(e=3) == [0,1,2,3]);
	assert(list_range(e=6, step=2) == [0,2,4,6]);
	assert(list_range(s=3, e=5) == [3,4,5]);
	assert(list_range(s=3, e=8, step=2) == [3,5,7]);
	assert(list_range(s=4, e=8, step=2) == [4,6,8]);
	assert(list_range(n=4, s=[3,4], step=[2,3]) == [[3,4], [5,7], [7,10], [9,13]]);
}
test_list_range();


module test_array_shortest() {
	assert(array_shortest(["foobar", "bazquxx", "abcd"]) == 4);
}
test_array_shortest();


module test_array_longest() {
	assert(array_longest(["foobar", "bazquxx", "abcd"]) == 7);
}
test_array_longest();


module test_array_pad() {
	assert(array_pad([4,5,6], 5, 8) == [4,5,6,8,8]);
	assert(array_pad([4,5,6,7,8], 5, 8) == [4,5,6,7,8]);
	assert(array_pad([4,5,6,7,8,9], 5, 8) == [4,5,6,7,8,9]);
}
test_array_pad();


module test_array_trim() {
	assert(array_trim([4,5,6], 5) == [4,5,6]);
	assert(array_trim([4,5,6,7,8], 5) == [4,5,6,7,8]);
	assert(array_trim([3,4,5,6,7,8,9], 5) == [3,4,5,6,7]);
}
test_array_trim();


module test_array_fit() {
	assert(array_fit([4,5,6], 5, 8) == [4,5,6,8,8]);
	assert(array_fit([4,5,6,7,8], 5, 8) == [4,5,6,7,8]);
	assert(array_fit([3,4,5,6,7,8,9], 5, 8) == [3,4,5,6,7]);
}
test_array_fit();


module test_enumerate() {
	assert(enumerate(["a","b","c"]) == [[0,"a"], [1,"b"], [2,"c"]]);
	assert(enumerate([[88,"a"],[76,"b"],[21,"c"]], idx=1) == [[0,"a"], [1,"b"], [2,"c"]]);
	assert(enumerate([["cat","a",12],["dog","b",10],["log","c",14]], idx=[1:2]) == [[0,"a",12], [1,"b",10], [2,"c",14]]);
}
test_enumerate();


module test_array_zip() {
	v1 = [1,2,3,4];
	v2 = [5,6,7];
	v3 = [8,9,10,11];
	assert(array_zip(v1,v3) == [[1,8],[2,9],[3,10],[4,11]]);
	assert(array_zip([v1,v3]) == [[1,8],[2,9],[3,10],[4,11]]);
	assert(array_zip([v1,v2],fit="short") == [[1,5],[2,6],[3,7]]);
	assert(array_zip([v1,v2],fit="long") == [[1,5],[2,6],[3,7],[4,undef]]);
	assert(array_zip([v1,v2],fit="long", fill=0) == [[1,5],[2,6],[3,7],[4,0]]);
	assert(array_zip([v1,v2,v3],fit="long") == [[1,5,8],[2,6,9],[3,7,10],[4,undef,11]]);
}
test_array_zip();


module test_array_group() {
	v = [1,2,3,4,5,6];
	assert(array_group(v,2) == [[1,2], [3,4], [5,6]]);
	assert(array_group(v,3) == [[1,2,3], [4,5,6]]);
	assert(array_group(v,4,0) == [[1,2,3,4], [5,6,0,0]]);
}
test_array_group();


module test_flatten() {
	assert(flatten([[1,2,3], [4,5,[6,7,8]]]) == [1,2,3,4,5,[6,7,8]]);
}
test_flatten();


module test_sort() {
	assert(sort([7,3,9,4,3,1,8]) == [1,3,3,4,7,8,9]);
	assert(sort(["cat", "oat", "sat", "bat", "vat", "rat", "pat", "mat", "fat", "hat", "eat"]) == ["bat", "cat", "eat", "fat", "hat", "mat", "oat", "pat", "rat", "sat", "vat"]);
	assert(sort(enumerate([[2,3,4],[1,2,3],[2,4,3]]),idx=1)==[[1,[1,2,3]], [0,[2,3,4]], [2,[2,4,3]]]);
}
test_sort();


module test_sortidx() {
	lst1 = ["d","b","e","c"];
	assert(sortidx(lst1) == [1,3,0,2]);
	lst2 = [
		["foo", 88, [0,0,1], false],
		["bar", 90, [0,1,0], true],
		["baz", 89, [1,0,0], false],
		["qux", 23, [1,1,1], true]
	];
	assert(sortidx(lst2, idx=1) == [3,0,2,1]);
	assert(sortidx(lst2, idx=0) == [1,2,0,3]);
	assert(sortidx(lst2, idx=[1,3]) == [3,0,2,1]);
	lst3 = [[-4, 0, 0], [0, 0, -4], [0, -4, 0], [-4, 0, 0], [0, -4, 0], [0, 0, 4], [0, 0, -4], [0, 4, 0], [4, 0, 0], [0, 0, 4], [0, 4, 0], [4, 0, 0]];
	assert(sortidx(lst3)==[0,3,2,4,1,6,5,9,7,10,8,11]);
}
test_sortidx();


module test_unique() {
	assert(unique([]) == []);
	assert(unique([8]) == [8]);
	assert(unique([7,3,9,4,3,1,8]) == [1,3,4,7,8,9]);
}
test_unique();


module test_array_dim() {
	assert(array_dim([[[1,2,3],[4,5,6]],[[7,8,9],[10,11,12]]]) == [2,2,3]);
	assert(array_dim([[[1,2,3],[4,5,6]],[[7,8,9],[10,11,12]]], 0) == 2);
	assert(array_dim([[[1,2,3],[4,5,6]],[[7,8,9],[10,11,12]]], 2) == 3);
	assert(array_dim([[[1,2,3],[4,5,6]],[[7,8,9]]]) == [2,undef,3]);
}
test_array_dim();


module test_vmul() {
	assert(vmul([3,4,5], [8,7,6]) == [24,28,30]);
	assert(vmul([1,2,3], [4,5,6]) == [4,10,18]);
}
test_vmul();


module test_vdiv() {
	assert(vdiv([24,28,30], [8,7,6]) == [3, 4, 5]);
}
test_vdiv();


module test_vabs() {
	assert(vabs([2,4,8]) == [2,4,8]);
	assert(vabs([-2,-4,-8]) == [2,4,8]);
	assert(vabs([-2,4,8]) == [2,4,8]);
	assert(vabs([2,-4,8]) == [2,4,8]);
	assert(vabs([2,4,-8]) == [2,4,8]);
}
test_vabs();


module test_normalize() {
	assert(normalize([10,0,0]) == [1,0,0]);
	assert(normalize([0,10,0]) == [0,1,0]);
	assert(normalize([0,0,10]) == [0,0,1]);
	assert(abs(norm(normalize([10,10,10]))-1) < eps);
	assert(abs(norm(normalize([-10,-10,-10]))-1) < eps);
	assert(abs(norm(normalize([-10,0,0]))-1) < eps);
	assert(abs(norm(normalize([0,-10,0]))-1) < eps);
	assert(abs(norm(normalize([0,0,-10]))-1) < eps);
}
test_normalize();


module test_vector_angle() {
	vecs = [[10,0,0], [-10,0,0], [0,10,0], [0,-10,0], [0,0,10], [0,0,-10]];
	for (a=vecs, b=vecs) {
		if(a==b) {
			assert(vector_angle(a,b)==0);
		} else if(a==-b) {
			assert(vector_angle(a,b)==180);
		} else {
			assert(vector_angle(a,b)==90);
		}
	}
	assert(abs(vector_angle([10,10,0],[10,0,0])-45) < eps);
}
test_vector_angle();


module test_vector_axis() {
	assert(norm(vector_axis([10,0,0],[10,10,0]) - [0,0,1]) < eps);
	assert(norm(vector_axis([10,0,0],[0,10,0]) - [0,0,1]) < eps);
	assert(norm(vector_axis([0,10,0],[10,0,0]) - [0,0,-1]) < eps);
	assert(norm(vector_axis([0,0,10],[10,0,0]) - [0,1,0]) < eps);
	assert(norm(vector_axis([10,0,0],[0,0,10]) - [0,-1,0]) < eps);
	assert(norm(vector_axis([10,0,10],[0,-10,0]) - [sin(45),0,-sin(45)]) < eps);
}
test_vector_axis();


module test_point2d() {
	assert(point2d([1,2,3])==[1,2]);
	assert(point2d([2,3])==[2,3]);
	assert(point2d([1])==[1,0]);
}
test_point2d();


module test_path2d() {
	assert(path2d([[1], [1,2], [1,2,3], [1,2,3,4], [1,2,3,4,5]])==[[1,0],[1,2],[1,2],[1,2],[1,2]]);
}
test_path2d();


module test_point3d() {
	assert(point3d([1,2,3,4,5])==[1,2,3]);
	assert(point3d([1,2,3,4])==[1,2,3]);
	assert(point3d([1,2,3])==[1,2,3]);
	assert(point3d([2,3])==[2,3,0]);
	assert(point3d([1])==[1,0,0]);
}
test_point3d();


module test_path3d() {
	assert(path3d([[1], [1,2], [1,2,3], [1,2,3,4], [1,2,3,4,5]])==[[1,0,0],[1,2,0],[1,2,3],[1,2,3],[1,2,3]]);
}
test_path3d();


module test_translate_points() {
	pts = [[0,0,1], [0,1,0], [1,0,0], [0,0,-1], [0,-1,0], [-1,0,0]];
	assert(translate_points(pts, v=[1,2,3]) == [[1,2,4], [1,3,3], [2,2,3], [1,2,2], [1,1,3], [0,2,3]]);
	assert(translate_points(pts, v=[-1,-2,-3]) == [[-1,-2,-2], [-1,-1,-3], [0,-2,-3], [-1,-2,-4], [-1,-3,-3], [-2,-2,-3]]);
}
test_translate_points();


module test_scale_points() {
	pts = [[0,0,1], [0,1,0], [1,0,0], [0,0,-1], [0,-1,0], [-1,0,0]];
	assert(scale_points(pts, v=[2,3,4]) == [[0,0,4], [0,3,0], [2,0,0], [0,0,-4], [0,-3,0], [-2,0,0]]);
	assert(scale_points(pts, v=[-2,-3,-4]) == [[0,0,-4], [0,-3,0], [-2,0,0], [0,0,4], [0,3,0], [2,0,0]]);
	assert(scale_points(pts, v=[1,1,1]) == [[0,0,1], [0,1,0], [1,0,0], [0,0,-1], [0,-1,0], [-1,0,0]]);
	assert(scale_points(pts, v=[-1,-1,-1]) == [[0,0,-1], [0,-1,0], [-1,0,0], [0,0,1], [0,1,0], [1,0,0]]);
}
test_scale_points();


module test_rotate_points2d() {
	pts = [[0,1], [1,0], [0,-1], [-1,0]];
	s = sin(45);
	assert(rotate_points2d(pts,45) == [[-s,s],[s,s],[s,-s],[-s,-s]]);
	assert(rotate_points2d(pts,90) == [[-1,0],[0,1],[1,0],[0,-1]]);
	assert(rotate_points2d(pts,90,cp=[1,0]) == [[0,-1],[1,0],[2,-1],[1,-2]]);
}
test_rotate_points2d();


module test_rotate_points3d() {
	pts = [[0,0,1], [0,1,0], [1,0,0], [0,0,-1], [0,-1,0], [-1,0,0]];
	assert(rotate_points3d(pts, [90,0,0]) == [[0,-1,0], [0,0,1], [1,0,0], [0,1,0], [0,0,-1], [-1,0,0]]);
	assert(rotate_points3d(pts, [0,90,0]) == [[1,0,0], [0,1,0], [0,0,-1], [-1,0,0], [0,-1,0], [0,0,1]]);
	assert(rotate_points3d(pts, [0,0,90]) == [[0,0,1], [-1,0,0], [0,1,0], [0,0,-1], [1,0,0], [0,-1,0]]);
	assert(rotate_points3d(pts, [0,0,90],cp=[2,0,0]) == [[2,-2,1], [1,-2,0], [2,-1,0], [2,-2,-1], [3,-2,0], [2,-3,0]]);
	assert(rotate_points3d(pts, 90, axis=V_UP) == [[0,0,1], [-1,0,0], [0,1,0], [0,0,-1], [1,0,0], [0,-1,0]]);
	assert(rotate_points3d(pts, 90, axis=V_DOWN) == [[0,0,1], [1,0,0], [0,-1,0], [0,0,-1], [-1,0,0], [0,1,0]]);
	assert(rotate_points3d(pts, 90, axis=V_RIGHT)  == [[0,-1,0], [0,0,1], [1,0,0], [0,1,0], [0,0,-1], [-1,0,0]]);
	assert(rotate_points3d(pts, from=V_UP, to=V_BACK) == [[0,1,0], [0,0,-1], [1,0,0], [0,-1,0], [0,0,1], [-1,0,0]]);
	assert(rotate_points3d(pts, 90, from=V_UP, to=V_BACK), [[0,1,0], [-1,0,0], [0,0,-1], [0,-1,0], [1,0,0], [0,0,1]]);
	assert(rotate_points3d(pts, from=V_UP, to=V_UP*2) == [[0,0,1], [0,1,0], [1,0,0], [0,0,-1], [0,-1,0], [-1,0,0]]);
	assert(rotate_points3d(pts, from=V_UP, to=V_DOWN*2) == [[0,0,-1], [0,1,0], [-1,0,0], [0,0,1], [0,-1,0], [1,0,0]]);
}
test_rotate_points3d();


module test_simplify_path()
{
	path = [[-20,10],[-10,0],[-5,0],[0,0],[5,0],[10,0], [10,10]];
	assert(simplify_path(path) == [[-20,10],[-10,0],[10,0], [10,10]]);
}
test_simplify_path();


module test_simplify_path_indexed()
{
	points = [[-20,10],[-10,0],[-5,0],[0,0],[5,0],[10,0], [10,10]];
	path = list_range(len(points));
	assert(simplify_path_indexed(points, path) == [0,1,5,6]);
}
test_simplify_path_indexed();


// vim: noexpandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
