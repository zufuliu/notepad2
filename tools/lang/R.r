# 4.2 https://www.r-project.org/
# https://cran.r-project.org/doc/manuals/r-release/R-lang.html
# https://cran.r-project.org/manuals.html

#! keywords
# https://cran.r-project.org/doc/manuals/r-release/R-lang.html#Reserved-words
if else repeat while function for in next break
TRUE FALSE NULL Inf NaN
NA NA_integer_ NA_real_ NA_complex_ NA_character_

#! package
# typeof()
# https://cran.r-project.org/doc/manuals/r-release/R-lang.html#Objects
any
builtin
bytecode
char
character
closure
complex
double
environment
expression
externalptr
integer
language
list
logical
NULL
pairlist
promise
raw
S4
special
symbol
weakref

# The R Base Package
# https://search.r-project.org/CRAN/doc/html/packages.html
.Autoloaded
.AutoloadEnv
.BaseNamespaceEnv
.bincode(x, breaks, right = TRUE, include.lowest = FALSE)
.C(.NAME, ..., NAOK = FALSE, DUP = TRUE, PACKAGE, ENCODING)
.Call(.NAME, ..., PACKAGE)
.class2(x)
.col(dim)
.colMeans(x, m, n, na.rm = FALSE)
.colSums(x, m, n, na.rm = FALSE)
.Defunct(new, package = NULL, msg)
.deparseOpts(control)
.Deprecated(new, package=NULL, msg, old = as.character(sys.call(sys.parent()))[1L])
.Device
.Devices
.doTrace(expr, msg)
.dynLibs(new)
.External(.NAME, ..., PACKAGE)
.format.zeros(x, zero.print, nx = suppressWarnings(as.numeric(x)), replace = FALSE, warn.non.fitting = TRUE)
.Fortran(.NAME, ..., NAOK = FALSE, DUP = TRUE, PACKAGE, ENCODING)
.GlobalEnv
.handleSimpleError(h, msg, call)
.Internal(call)
.isOpen(srcfile)
.kappa_tri(z, exact = FALSE, LINPACK = TRUE, norm = NULL, ...)
.Last.lib(libpath)
.Last.value
.libPaths(new)
.Library
.Library.site
.Machine
.makeMessage(..., domain = NULL, appendLF = FALSE)
.NotYetImplemented()
.NotYetUsed(arg, error = TRUE)
.onAttach(libname, pkgname)
.onDetach(libpath)
.onLoad(libname, pkgname)
.onUnload(libpath)
.Options
.packages(all.available = FALSE, lib.loc = NULL)
.Platform
.Primitive(name)
.Random.seed <- c(rng.kind, n1, n2, ...)
.row(dim)
.rowMeans(x, m, n, na.rm = FALSE)
.rowNamesDF(x, make.names=FALSE) <- value
.rowSums(x, m, n, na.rm = FALSE)
.S3method(generic, class, method)
.signalSimpleWarning(msg, call)
.standard_regexps()
.traceback(x = NULL, max.lines = getOption("traceback.max.lines", getOption("deparse.max.lines", -1L)))
.tryResumeInterrupt()
abbreviate(names.arg, minlength = 4, use.classes = TRUE, dot = FALSE, strict = FALSE, method = c("left.kept", "both.sides"), named = TRUE)
abs(x)
acos(x)
activeBindingFunction(sym, env)
addNA(x, ifany = FALSE)
addTaskCallback(f, data = NULL, name = character())
agrep(pattern, x, max.distance = 0.1, costs = NULL, ignore.case = FALSE, value = FALSE, fixed = TRUE, useBytes = FALSE)
agrepl(pattern, x, max.distance = 0.1, costs = NULL, ignore.case = FALSE, fixed = TRUE, useBytes = FALSE)
alist(...)
all(..., na.rm = FALSE)
all.equal(target, current, ...)
all.names(expr, functions = TRUE, max.names = -1L, unique = FALSE)
all.vars(expr, functions = FALSE, max.names = -1L, unique = TRUE)
allowInterrupts(expr)
any(..., na.rm = FALSE)
anyDuplicated(x, incomparables = FALSE, ...)
anyNA(x, recursive = FALSE)
aperm(a, perm, ...)
append(x, values, after = length(x))
apply(X, MARGIN, FUN, ...)
Arg(z)
args(name)
array(data = NA, dim = length(data), dimnames = NULL)
arrayInd(ind, .dim, .dimnames = NULL, useNames = FALSE)
as.array(x, ...)
as.call(x)
as.character(x, ...)
as.complex(x, ...)
as.data.frame(x, row.names = NULL, optional = FALSE, ..., cut.names = FALSE, col.names = names(x), fix.empty.names = TRUE, stringsAsFactors = default.stringsAsFactors())
as.Date(x, tz = "UTC", ...)
as.difftime(tim, format = "%X", units = "auto", tz = "UTC")
as.double(x, ...)
as.environment(x)
as.expression(x, ...)
as.factor(x)
as.function(x, ...)
as.hexmode(x)
as.integer(x, ...)
as.list(x, ...)
as.logical(x, ...)
as.matrix(x, ...)
as.name(x)
as.null(x, ...)
as.numeric(x, ...)
as.octmode(x)
as.ordered(x)
as.pairlist(x)
as.POSIXct(x, tz = "", ...)
as.POSIXlt(x, tz = "", ...)
as.qr(x)
as.raw(x)
as.single(x, ...)
as.symbol(x)
as.table(x, ...)
as.vector(x, mode = "any")
asin(x)
asplit(x, MARGIN)
asS3(object, flag = TRUE, complete = TRUE)
asS4(object, flag = TRUE, complete = TRUE)
assign(x, value, pos = -1, envir = as.environment(pos), inherits = FALSE, immediate = TRUE)
atan(x)
atan2(y, x)
attach(what, pos = 2L, name = deparse1(substitute(what), backtick=FALSE), warn.conflicts = TRUE)
attachNamespace(ns, pos = 2L, depends = NULL, exclude, include.only)
attr(x, which, exact = FALSE)
attr.all.equal(target, current, ...)
attributes(x)
autoload(name, package, reset = FALSE, ...)
autoloader(name, package, ...)
backsolve(r, x, k = ncol(r), upper.tri = TRUE, transpose = FALSE)
baseenv()
basename(path)
besselI(x, nu, expon.scaled = FALSE)
besselJ(x, nu)
besselK(x, nu, expon.scaled = FALSE)
besselY(x, nu)
beta(a, b)
bindingIsActive(sym, env)
bindingIsLocked(sym, env)
bindtextdomain(domain, dirname = NULL)
bitwAnd(a, b)
bitwNot(a)
bitwOr(a, b)
bitwShiftL(a, n)
bitwShiftR(a, n)
bitwXor(a, b)
body(fun = sys.function(sys.parent()))
body(fun, envir = environment(fun)) <- value
bquote(expr, where = parent.frame(), splice = FALSE)
browser(text = "", condition = NULL, expr = TRUE, skipCalls = 0L)
browserCondition(n = 1)
browserSetDebug(n = 1)
browserText(n = 1)
by(data, INDICES, FUN, ..., simplify = TRUE)
bzfile(description, open = "", encoding = getOption("encoding"), compression = 9)
call(name, ...)
callCC(fun)
capabilities(what = NULL, Xchk = any(nas %in% c("X11", "jpeg", "png", "tiff")))
casefold(x, upper = FALSE)
cat(... , file = "", sep = " ", fill = FALSE, labels = NULL, append = FALSE)
cbind(..., deparse.level = 1)
ceiling(x)
char.expand(input, target, nomatch = stop("no match"))
character(length = 0)
charmatch(x, table, nomatch = NA_integer_)
charToRaw(x)
chartr(old, new, x)
chkDots(..., which.call = -1, allowed = character(0))
chol(x, ...)
chol2inv(x, size = NCOL(x), LINPACK = FALSE)
choose(n, k)
class(x)
class(x) <- value
clearPushBack(connection)
close(con, ...)
closeAllConnections()
col(x, as.factor = FALSE)
colMeans(x, na.rm = FALSE, dims = 1)
colnames(x, do.NULL = TRUE, prefix = "col")
colSums(x, na.rm = FALSE, dims = 1)
commandArgs(trailingOnly = FALSE)
comment(x)
comment(x) <- value
complex(length.out = 0, real = numeric(), imaginary = numeric(), modulus = 1, argument = 0)
Complex(z)
computeRestarts(cond = NULL)
conditionCall(c)
conditionMessage(c)
conflictRules(pkg, mask.ok = NULL, exclude = NULL)
conflicts(where = search(), detail = FALSE)
Conj(z)
contributors()
cos(x)
cospi(x)
crossprod(x, y = NULL)
Cstack_info()
cummax(x)
cummin(x)
cumprod(x)
cumsum(x)
curlGetHeaders(url, redirect = TRUE, verify = TRUE)
cut(x, ...)
data.class(x)
data.frame(..., row.names = NULL, check.rows = FALSE, check.names = TRUE, fix.empty.names = TRUE, stringsAsFactors = default.stringsAsFactors())
data.matrix(frame, rownames.force = NA)
date()
debug(fun, text = "", condition = NULL, signature = NULL)
debuggingState(on = NULL)
debugonce(fun, text = "", condition = NULL, signature = NULL)
delayedAssign(x, value, eval.env = parent.frame(1), assign.env = parent.frame(1))
deparse(expr, width.cutoff = 60L, backtick = mode(expr) %in% c("call", "expression", "(", "function"), control = c("keepNA", "keepInteger", "niceNames", "showAttributes"), nlines = -1L)
deparse1(expr, collapse = " ", width.cutoff = 500L, ...)
det(x, ...)
detach(name, pos = 2L, unload = FALSE, character.only = FALSE, force = FALSE)
determinant(x, logarithm = TRUE, ...)
dget(file, keep.source = FALSE)
diag(x = 1, nrow, ncol, names = TRUE)
diff(x, ...)
difftime(time1, time2, tz, ...)
digamma(x)
dim(x)
dimnames(x)
dir(path = ".", pattern = NULL, all.files = FALSE, ...)
dir.create(path, showWarnings = TRUE, recursive = FALSE, mode = "0777")
dir.exists(paths)
dirname(path)
do.call(what, args, quote = FALSE, envir = parent.frame())
dontCheck(x)
double(length = 0)
dQuote(x, q = getOption("useFancyQuotes"))
drop(x)
droplevels(x, except, exclude, ...)
dump(list, file = "dumpdata.R", append = FALSE, control = "all", envir = parent.frame(), evaluate = TRUE)
duplicated(x, incomparables = FALSE, ...)
dyn.load(x, local = TRUE, now = TRUE, ...)
dyn.unload(x)
dynGet(x, ifnotfound = , minframe = 1L, inherits = FALSE)
dynLibs(new)
eapply(env, FUN, ..., all.names = FALSE, USE.NAMES = TRUE)
eigen(x, symmetric, only.values = FALSE, EISPACK = FALSE)
emptyenv()
enc2native(x)
enc2utf8(x)
encodeString(x, width = 0, quote = "", na.encode = TRUE, justify = c("left", "right", "centre", "none"))
Encoding(x)
endsWith(x, suffix)
enquote(cl)
env.profile(env)
environment(fun = NULL)
environmentIsLocked(env)
environmentName(env)
errorCondition(message, ..., class = NULL, call = NULL)
eval(expr, envir = parent.frame(), enclos = if(is.list(envir) || is.pairlist(envir)) parent.frame() else baseenv())
eval.parent(expr, n = 1)
evalq(expr, envir, enclos)
exists(x, where = -1, envir = , frame, mode = "any", inherits = TRUE)
exp(x)
expand.grid(..., KEEP.OUT.ATTRS = TRUE, stringsAsFactors = TRUE)
expm1(x)
expression(...)
extSoftVersion()
factor(x = character(), levels, labels = levels, exclude = NA, ordered = is.ordered(x), nmax = NA)
factorial(x)
fifo(description, open = "", blocking = FALSE, encoding = getOption("encoding"))
file(description = "", open = "", blocking = TRUE, encoding = getOption("encoding"), raw = FALSE, method = getOption("url.method", "default"))
file.access(names, mode = 0)
file.append(file1, file2)
file.choose(new = FALSE)
file.copy(from, to, overwrite = recursive, recursive = FALSE, copy.mode = TRUE, copy.date = FALSE)
file.create(..., showWarnings = TRUE)
file.exists(...)
file.info(..., extra_cols = TRUE)
file.link(from, to)
file.mode(...)
file.mtime(...)
file.path(..., fsep = .Platform$file.sep)
file.remove(...)
file.rename(from, to)
file.show(..., header = rep("", nfiles), title = "R Information", delete.file = FALSE, pager = getOption("pager"), encoding = "")
file.size(...)
file.symlink(from, to)
Filter(f, x)
Find(f, x, right = FALSE, nomatch = NULL)
find.package(package, lib.loc = NULL, quiet = FALSE, verbose = getOption("verbose"))
findInterval(x, vec, rightmost.closed = FALSE, all.inside = FALSE, left.open = FALSE)
findRestart(name, cond = NULL)
floor(x)
flush(con)
force(x)
forceAndCall(n, FUN, ...)
formals(fun = sys.function(sys.parent()), envir = parent.frame())
formals(fun, envir = environment(fun)) <- value
format(x, ...)
format.info(x, digits = NULL, nsmall = 0)
format.pval(pv, digits = max(1, getOption("digits") - 2), eps = .Machine$double.eps, na.form = "NA", ...)
formatC(x, digits = NULL, width = NULL, format = NULL, flag = "", mode = NULL, big.mark = "", big.interval = 3L, small.mark = "", small.interval = 5L, decimal.mark = getOption("OutDec"), preserve.width = "individual", zero.print = NULL, replace.zero = TRUE, drop0trailing = FALSE)
formatDL(x, y, style = c("table", "list"), width = 0.9 * getOption("width"), indent = NULL)
forwardsolve(l, x, k = ncol(l), upper.tri = FALSE, transpose = FALSE)
gamma(x)
gc(verbose = getOption("verbose"), reset = FALSE, full = TRUE)
gc.time(on = TRUE)
gcinfo(verbose)
gctorture(on = TRUE)
gctorture2(step, wait = step, inhibit_release = FALSE)
get(x, pos = -1, envir = as.environment(pos), mode = "any", inherits = TRUE)
get0(x, envir = pos.to.env(-1L), mode = "any", inherits = TRUE, ifnotfound = NULL)
getConnection(what)
getDLLRegisteredRoutines(dll, addNames = TRUE)
geterrmessage()
getHook(hookName)
getLoadedDLLs()
getNativeSymbolInfo(name, PACKAGE, unlist = TRUE, withRegistrationInfo = FALSE)
getOption(x, default = NULL)
getRversion()
getSrcLines(srcfile, first, last)
getTaskCallbackNames()
gettext(..., domain = NULL)
gettextf(fmt, ..., domain = NULL)
getwd()
gl(n, k, length = n*k, labels = seq_len(n), ordered = FALSE)
globalCallingHandlers(...)
globalenv()
gregexpr(pattern, text, ignore.case = FALSE, perl = FALSE, fixed = FALSE, useBytes = FALSE)
grep(pattern, x, ignore.case = FALSE, perl = FALSE, value = FALSE, ...E)
grepl(pattern, x, ignore.case = FALSE, perl = FALSE, fixed = FALSE, useBytes = FALSE)
grepRaw(pattern, x, offset = 1L, ignore.case = FALSE, ...)
grouping(...)
gsub(pattern, replacement, x, ignore.case = FALSE, perl = FALSE, fixed = FALSE, useBytes = FALSE)
gzcon(con, level = 6, allowNonCompressed = TRUE, text = FALSE)
gzfile(description, open = "", encoding = getOption("encoding"), compression = 6)
iconv(x, from = "", to = "", sub = NA, mark = TRUE, toRaw = FALSE)
iconvlist()
icuGetCollate(type = c("actual", "valid"))
icuSetCollate(...)
identical(x, y, num.eq = TRUE, single.NA = TRUE, attrib.as.set = TRUE, ignore.bytecode = TRUE, ignore.environment = FALSE, ignore.srcref = TRUE)
identity(x)
ifelse(test, yes, no)
Im(z)
infoRDS(file)
inherits(x, what, which = FALSE)
integer(length = 0)
interaction(..., drop = FALSE, sep = ".", lex.order = FALSE)
interactive()
intersect(x, y)
intToBits(x)
intToUtf8(x, multiple = FALSE, allow_surrogate_pairs = FALSE)
inverse.rle(x, ...)
invisible(x)
invokeRestart(r, ...)
invokeRestartInteractively(r)
is.array(x)
is.atomic(x)
is.call(x)
is.character(x)
is.complex(x)
is.data.frame(x)
is.double(x)
is.element(el, set)
is.environment(x)
is.expression(x)
is.factor(x)
is.finite(x)
is.function(x)
is.infinite(x)
is.integer(x)
is.language(x)
is.list(x)
is.loaded(symbol, PACKAGE = "", type = "")
is.logical(x)
is.matrix(x)
is.na(x)
is.name(x)
is.nan(x)
is.null(x)
is.numeric(x)
is.object(x)
is.ordered(x)
is.pairlist(x)
is.primitive(x)
is.qr(x)
is.R()
is.raw(x)
is.recursive(x)
is.symbol(x)
is.table(x)
is.unsorted(x, na.rm = FALSE, strictly = FALSE)
is.vector(x, mode = "any")
isatty(con)
isdebugged(fun, signature = NULL)
isFALSE(x)
isIncomplete(con)
isNamespaceLoaded(name)
ISOdate(year, month, day, hour = 12, min = 0, sec = 0, tz = "GMT")
ISOdatetime(year, month, day, hour, min, sec, tz = "")
isOpen(con, rw = "")
isRestart(x)
isS4(object)
isSeekable(con)
isSymmetric(object, ...)
isTRUE (x)
isTRUE(x)
jitter(x, factor = 1, amount = NULL)
julian(x, ...)
kappa(z, ...)
kronecker(X, Y, FUN = "*", make.dimnames = FALSE, ...)
l10n_info()
La.svd(x, nu = min(n, p), nv = min(n, p))
La_library()
La_version()
labels(object, ...)
lapply(X, FUN, ...)
lbeta(a, b)
lchoose(n, k)
length(x)
length(x) <- value
lengths(x, use.names = TRUE)
letters
LETTERS
levels(x)
levels(x) <- value
lfactorial(x)
lgamma(x)
libcurlVersion()
library(package, help, pos = 2, lib.loc = NULL, character.only = FALSE, logical.return = FALSE, warn.conflicts, quietly = FALSE, verbose = getOption("verbose"), mask.ok, exclude, include.only, attach.required = missing(include.only))
library.dynam(chname, package, lib.loc, verbose = getOption("verbose"), file.ext = .Platform$dynlib.ext, ...)
library.dynam.unload(chname, libpath, verbose = getOption("verbose"), file.ext = .Platform$dynlib.ext)
licence()
license()
list(...)
list.dirs(path = ".", full.names = TRUE, recursive = TRUE)
list.files(path = ".", pattern = NULL, all.files = FALSE, full.names = FALSE, recursive = FALSE, ignore.case = FALSE, include.dirs = FALSE, no.. = FALSE)
list2DF(x = list(), nrow = NULL)
list2env(x, envir = NULL, parent = parent.frame(), hash = (length(x) > 100), size = max(29L, length(x)))
load(file, envir = parent.frame(), verbose = FALSE)
loadedNamespaces()
loadNamespace(package, lib.loc = NULL, keep.source = getOption("keep.source.pkgs"), partial = FALSE, versionCheck = NULL, keep.parse.data = getOption("keep.parse.data.pkgs"))
local(expr, envir = new.env())
lockBinding(sym, env)
lockEnvironment(env, bindings = FALSE)
log(x, base = exp(1))
log10(x)
log1p(x)
log2(x)
logb(x, base = exp(1))
logical(length = 0)
lower.tri(x, diag = FALSE)
ls(name, pos = -1L, envir = as.environment(pos), all.names = FALSE, pattern, sorted = TRUE)
make.names(names, unique = FALSE, allow_ = TRUE)
make.unique(names, sep = ".")
makeActiveBinding(sym, fun, env)
Map(f, ...)
mapply(FUN, ..., MoreArgs = NULL, SIMPLIFY = TRUE, USE.NAMES = TRUE)
margin.table(x, margin = NULL)
marginSums(x, margin = NULL)
mat.or.vec(nr, nc)
match(x, table, nomatch = NA_integer_, incomparables = NULL)
match.arg(arg, choices, several.ok = FALSE)
match.call(definition = sys.function(sys.parent()), call = sys.call(sys.parent()), expand.dots = TRUE, envir = parent.frame(2L))
match.fun(FUN, descend = TRUE)
Math(x, ...)
matrix(data = NA, nrow = 1, ncol = 1, byrow = FALSE, )
max(..., na.rm = FALSE)
max.col(m, ties.method = c("random", "first", "last"))
mean(x, ...)
mem.maxNSize(nsize = 0)
mem.maxVSize(vsize = 0)
memCompress(from, type = c("gzip", "bzip2", "xz", "none"))
memDecompress(from, type = c("unknown", "gzip", "bzip2", "xz", "none"), asChar = FALSE)
memory.profile()
merge(x, y, ...)
message(..., domain = NULL, appendLF = TRUE)
mget(x, envir = as.environment(-1), mode = "any", ifnotfound, inherits = FALSE)
min(..., na.rm = FALSE)
missing(x)
Mod(z)
mode(x)
month.abb
month.name
months(x, abbreviate)
mostattributes(x) <- value
names(x)
nargs()
nchar(x, type = "chars", allowNA = FALSE, keepNA = NA)
ncol(x)
NCOL(x)
Negate(f)
new.env(hash = TRUE, parent = parent.frame(), size = 29L)
NextMethod(generic = NULL, object = NULL, ...)
ngettext(n, msg1, msg2, domain = NULL)
nlevels(x)
noquote(obj, right = FALSE)
norm(x, type = c("O", "I", "F", "M", "2"))
normalizePath(path, winslash = "\\", mustWork = NA)
nrow(x)
NROW(x)
nullfile()
numeric(length = 0)
numeric_version(x, strict = TRUE)
nzchar(x, keepNA = FALSE)
objects(name, pos= -1L, envir = as.environment(pos), all.names = FALSE, pattern, sorted = TRUE)
oldClass(x)
oldClass(x) <- value
OlsonNames(tzdir = NULL)
on.exit(expr = NULL, add = FALSE, after = TRUE)
open(con, ...)
Ops(e1, e2)
options(...)
order(..., na.last = TRUE, decreasing = FALSE, method = c("auto", "shell", "radix"))
ordered(x, ...)
outer(X, Y, FUN = "*", ...)
package_version(x, strict = TRUE)
packageEvent(pkgname, event = c("onLoad", "attach", "detach", "onUnload"))
packageNotFoundError(package, lib.loc, call = NULL)
packageStartupMessage(..., domain = NULL, appendLF = TRUE)
packBits(x, type = c("raw", "integer"))
pairlist(...)
parent.env(env)
parent.frame(n = 1)
parse(file = "", n = NULL, text = NULL, prompt = "?", keep.source = getOption("keep.source"), srcfile, encoding = "unknown")
paste(..., sep = " ", collapse = NULL, recycle0 = FALSE)
paste0(..., collapse = NULL, recycle0 = FALSE)
path.expand(path)
path.package(package, quiet = FALSE)
pcre_config()
pi
pipe(description, open = "", encoding = getOption("encoding"))
plot(x, y, ...)
pmatch(x, table, nomatch = NA_integer_, duplicates.ok = FALSE)
pmax(..., na.rm = FALSE)
pmax.int(..., na.rm = FALSE)
pmin(..., na.rm = FALSE)
pmin.int(..., na.rm = FALSE)
polyroot(z)
pos.to.env(x)
Position(f, x, right = FALSE, nomatch = NA_integer_)
pretty(x, ...)
prettyNum(x, big.mark = "",  big.interval = 3L, small.mark  = "", small.interval = 5L, decimal.mark = getOption("OutDec"), input.d.mark = decimal.mark, preserve.width = c("common", "individual", "none"), zero.print = NULL, replace.zero = FALSE, drop0trailing = FALSE, is.cmplx = NA,)
print(x, ...)
prmatrix(x, rowlab =, collab =, quote = TRUE, right = FALSE, na.print = NULL, ...)
proc.time()
prod(..., na.rm = FALSE)
prop.table(x, margin = NULL)
proportions(x, margin = NULL)
provideDimnames(x, sep = "", base = list(LETTERS), unique = TRUE)
psigamma(x, deriv = 0)
pushBack(data, connection, newLine = TRUE, encoding = c("", "bytes", "UTF-8"))
pushBackLength(connection)
qr(x, ...)
qr.coef(qr, y)
qr.fitted(qr, y, k = qr$rank)
qr.Q(qr, complete = FALSE, Dvec =)
qr.qty(qr, y)
qr.qy(qr, y)
qr.R(qr, complete = FALSE)
qr.resid(qr, y)
qr.solve(a, b, tol = 1e-7)
qr.X(qr, complete = FALSE, ncol =)
quarters(x, ...)
quit(save = "default", status = 0, runLast = TRUE)
quote(expr)
R.home(component = "home")
R.version
R.Version()
R.version.string
R_system_version(x, strict = TRUE)
range(..., na.rm = FALSE)
rank(x, na.last = TRUE, ...)
rapply(object, f, classes = "ANY", deflt = NULL, how = c("unlist", "replace", "list"), ...)
raw(length = 0)
rawConnection(object, open = "r")
rawConnectionValue(con)
rawShift(x, n)
rawToBits(x)
rawToChar(x, multiple = FALSE)
rbind(..., deparse.level = 1)
rcond(x, norm = c("O","I","1"), triangular = FALSE, ...)
Re(z)
read.dcf(file, fields = NULL, all = FALSE, keep.white = NULL)
readBin(con, what, n = 1L, size = NA_integer_, signed = TRUE, endian = .Platform$endian)
readChar(con, nchars, useBytes = FALSE)
readline(prompt = "")
readLines(con = stdin(), n = -1L, ok = TRUE, warn = TRUE, encoding = "unknown", skipNul = FALSE)
readRDS(file, refhook = NULL)
readRenviron(path)
Recall(...)
Reduce(f, x, init, right = FALSE, accumulate = FALSE)
reg.finalizer(e, f, onexit = FALSE)
regexec(pattern, text, ignore.case = FALSE, perl = FALSE, fixed = FALSE, useBytes = FALSE)
regexpr(pattern, text, ignore.case = FALSE, perl = FALSE, fixed = FALSE, useBytes = FALSE)
regmatches(x, m, invert = FALSE)
remove(..., list = character(), pos = -1, envir = as.environment(pos), inherits = FALSE)
removeTaskCallback(id)
rep(x, ...)
rep.int(x, times)
rep_len(x, length.out)
replace(x, list, values)
replicate(n, expr, simplify = "array")
require(package, lib.loc = NULL, quietly = FALSE, warn.conflicts, character.only = FALSE, mask.ok, exclude, include.only, attach.required = missing(include.only))
requireNamespace(package, ..., quietly = FALSE)
restartDescription(r)
restartFormals(r)
retracemem(x, previous = NULL)
returnValue(default = NULL)
rev(x)
rle(x)
rm(..., list = character(), pos = -1, envir = as.environment(pos), inherits = FALSE)
RNGkind(kind = NULL, normal.kind = NULL, sample.kind = NULL)
RNGversion(vstr)
round(x, digits = 0)
row(x, as.factor = FALSE)
row.names(x)
rowMeans(x, na.rm = FALSE, dims = 1)
rownames(x, do.NULL = TRUE, prefix = "row")
rowsum(x, group, reorder = TRUE, ...)
rowSums(x, na.rm = FALSE, dims = 1)
sample(x, size, replace = FALSE, prob = NULL)
sample.int(n, size = n, replace = FALSE, prob = NULL, useHash = (!replace && is.null(prob) && size <= n/2 && n > 1e7))
sapply(X, FUN, ..., simplify = TRUE, USE.NAMES = TRUE)
save(..., list = character(), file = stop("'file' must be specified"), ascii = FALSE, version = NULL, envir = parent.frame(), compress = isTRUE(!ascii), compression_level, eval.promises = TRUE, precheck = TRUE)
save.image(file = ".RData", version = NULL, ascii = FALSE, compress = !ascii, safe = TRUE)
saveRDS(object, file = "", ascii = FALSE, version = NULL, compress = TRUE, refhook = NULL)
scale(x, center = TRUE, scale = TRUE)
scan(file = "", what = double(), nmax = -1, n = -1, sep = "", quote = if(identical(sep, "\n")) "" else "'\"", dec = ".", skip = 0, nlines = 0, na.strings = "NA", flush = FALSE, fill = FALSE, strip.white = FALSE, quiet = FALSE, blank.lines.skip = TRUE, multi.line = TRUE, comment.char = "", allowEscapes = FALSE, fileEncoding = "", encoding = "unknown", text, skipNul = FALSE)
search()
searchpaths()
seek(con, ...)
seq(...)
seq.int(from, to, by, length.out, along.with, ...)
seq_along(along.with)
seq_len(length.out)
sequence(nvec, ...)
serialize(object, connection, ascii, xdr = TRUE, version = NULL, refhook = NULL)
serverSocket(port)
set.seed(seed, kind = NULL, normal.kind = NULL, sample.kind = NULL)
setdiff(x, y)
setequal(x, y)
setHook(hookName, value, ...)
setSessionTimeLimit(cpu = Inf, elapsed = Inf)
setTimeLimit(cpu = Inf, elapsed = Inf, transient = FALSE)
setwd(dir)
shell(cmd, shell, flag = "/c", intern = FALSE, wait = TRUE, translate = FALSE, mustWork = FALSE, ...)
shell.exec(file)
showConnections(all = FALSE)
shQuote(string, type = c("sh", "csh", "cmd", "cmd2"))
sign(x)
signalCondition(cond)
signif(x, digits = 6)
simpleCondition(message, call = NULL)
simpleError(message, call = NULL)
simpleMessage(message, call = NULL)
simpleWarning(message, call = NULL)
simplify2array(x, higher = TRUE)
sin(x)
single(length = 0)
sink(file = NULL, append = FALSE, type = c("output", "message"), split = FALSE)
sink.number(type = c("output", "message"))
sinpi(x)
slice.index(x, MARGIN)
socketAccept(socket, blocking = FALSE, open = "a+", encoding = getOption("encoding"), timeout = getOption("timeout"))
socketConnection(host = "localhost", port, server = FALSE, blocking = FALSE, open = "a+", encoding = getOption("encoding"), timeout = getOption("timeout"))
socketSelect(socklist, write = FALSE, timeout = NULL)
socketTimeout(socket, timeout = -1)
solve(a, b, ...)
sort(x, decreasing = FALSE, ...)
sort.int(x, partial = NULL, na.last = NA, decreasing = FALSE, method = c("auto", "shell", "quick", "radix"), index.return = FALSE)
sort.list(x, partial = NULL, na.last = TRUE, decreasing = FALSE, method = c("auto", "shell", "quick", "radix"))
source(file, local = FALSE, echo = verbose, print.eval = echo, exprs, spaced = use_file, verbose = getOption("verbose"), prompt.echo = getOption("prompt"), max.deparse.length = 150, width.cutoff = 60L, deparseCtrl = "showAttributes", chdir = FALSE, encoding = getOption("encoding"), continue.echo = getOption("continue"), skip.echo = 0, keep.source = getOption("keep.source"))
split(x, f, drop = FALSE, ...)
sprintf(fmt, ...)
sqrt(x)
sQuote(x, q = getOption("useFancyQuotes"))
srcfile(filename, encoding = getOption("encoding"), Enc = "unknown")
srcfilealias(filename, srcfile)
srcfilecopy(filename, lines, timestamp = Sys.time(), isFile = FALSE)
srcref(srcfile, lloc)
standardGeneric(f, fdef)
startsWith(x, prefix)
stderr()
stdin()
stdout()
stop(..., call. = TRUE, domain = NULL)
stopifnot(..., exprs, exprObject, local = TRUE)
storage.mode(x)
storage.mode(x) <- value
str2expression(text)
str2lang(s)
strrep(x, times)
strsplit(x, split, fixed = FALSE, perl = FALSE, useBytes = FALSE)
strtoi(x, base = 0L)
strtrim(x, width)
structure(.Data, ...)
strwrap(x, width = 0.9 * getOption("width"), indent = 0, exdent = 0, prefix = "", simplify = TRUE, initial = prefix)
sub(pattern, replacement, x, ignore.case = FALSE, perl = FALSE, fixed = FALSE, useBytes = FALSE)
subset(x, ...)
substitute(expr, env)
substr(x, start, stop)
substring(text, first, last = 1000000L)
sum(..., na.rm = FALSE)
Summary(..., na.rm = FALSE)
summary(object, ...)
suppressMessages(expr, classes = "message")
suppressPackageStartupMessages(expr)
suppressWarnings(expr, classes = "warning")
suspendInterrupts(expr)
svd(x, nu = min(n, p), nv = min(n, p), LINPACK = FALSE)
sweep(x, MARGIN, STATS, FUN = "-", check.margin = TRUE, ...)
sys.call(which = 0)
sys.calls()
Sys.chmod(paths, mode = "0777", use_umask = TRUE)
Sys.Date()
sys.frame(which = 0)
sys.frames()
sys.function(which = 0)
Sys.getenv(x = NULL, unset = "", names = NA)
Sys.getlocale(category = "LC_ALL")
Sys.getpid()
Sys.glob(paths, dirmark = FALSE)
Sys.info()
Sys.junction(from, to)
Sys.localeconv()
sys.nframe()
sys.on.exit()
sys.parent(n = 1)
sys.parents()
Sys.readlink(paths)
Sys.setenv(...)
Sys.setFileTime(path, time)
Sys.setlocale(category = "LC_ALL", locale = "")
Sys.sleep(time)
sys.source(file, envir = baseenv(), chdir = FALSE, keep.source = getOption("keep.source.pkgs"), keep.parse.data = getOption("keep.parse.data.pkgs"), toplevel.env = as.environment(envir))
sys.status()
Sys.time()
Sys.timezone(location = TRUE)
Sys.umask(mode = NA)
Sys.unsetenv(x)
Sys.which(names)
system(command, intern = FALSE, ignore.stdout = FALSE, ignore.stderr = FALSE, wait = TRUE, input = NULL, show.output.on.console = TRUE, minimized = FALSE, invisible = TRUE, timeout = 0)
system.file(..., package = "base", lib.loc = NULL, mustWork = FALSE)
system.time(expr, gcFirst = TRUE)
system2(command, args = character(), stdout = "", stderr = "", stdin = "", input = NULL, env = character(), wait = TRUE, minimized = FALSE, invisible = TRUE, timeout = 0)
table(..., exclude = if (useNA == "no") c(NA, NaN), useNA = c("no", "ifany", "always"), dnn = list.names(...), deparse.level = 1)
tabulate(bin, nbins = max(1, bin, na.rm = TRUE))
tan(x)
tanpi(x)
tapply(X, INDEX, FUN = NULL, ..., default = NA, simplify = TRUE)
taskCallbackManager(handlers = list(), registered = FALSE, verbose = FALSE)
tcrossprod(x, y = NULL)
tempdir(check = FALSE)
tempfile(pattern = "file", tmpdir = tempdir(), fileext = "")
textConnection(object, open = "r", local = FALSE, encoding = c("", "bytes", "UTF-8"))
textConnectionValue(con)
tolower(x)
topenv(envir = parent.frame(), matchThisEnv = getOption("topLevelEnvironment"))
toString(x, ...)
toupper(x)
trace(what, tracer, exit, at, print, signature, where = topenv(parent.frame()), edit = FALSE)
traceback(x = NULL, max.lines = getOption("traceback.max.lines", getOption("deparse.max.lines", -1L)))
tracemem(x)
tracingState(on = NULL)
transform(`_data`, ...)
trigamma(x)
trimws(x, which = c("both", "left", "right"), whitespace = "[ \t\r\n]")
trunc(x, ...)
truncate(con, ...)
try(expr, silent = FALSE, outFile = getOption("try.outFile", default = stderr()))
tryCatch(expr, ..., finally)
tryInvokeRestart(r, ...)
typeof(x)
unclass(x)
undebug(fun, signature = NULL)
union(x, y)
unique(x, incomparables = FALSE, ...)
unlink(x, recursive = FALSE, force = FALSE, expand = TRUE)
unlist(x, recursive = TRUE, use.names = TRUE)
unloadNamespace(ns)
unlockBinding(sym, env)
unname(obj, force = FALSE)
unserialize(connection, refhook = NULL)
unsplit(value, f, drop = FALSE)
untrace(what, signature = NULL, where = topenv(parent.frame()))
untracemem(x)
unz(description, filename, open = "", encoding = getOption("encoding"))
upper.tri(x, diag = FALSE)
url(description, open = "", blocking = TRUE, encoding = getOption("encoding"), method = getOption("url.method", "default"), headers = NULL)
UseMethod(generic, object)
utf8ToInt(x)
validEnc(x)
validUTF8(x)
vapply(X, FUN, FUN.VALUE, ..., USE.NAMES = TRUE)
vector(mode = "logical", length = 0)
Vectorize(FUN, vectorize.args = arg.names, SIMPLIFY = TRUE, USE.NAMES = TRUE)
version
warning(..., call. = TRUE, immediate. = FALSE, noBreaks. = FALSE, domain = NULL)
warningCondition(message, ..., class = NULL, call = NULL)
weekdays(x, abbreviate)
which(x, arr.ind = FALSE, useNames = TRUE)
with(data, expr, ...)
withAutoprint(exprs, evaluated = FALSE, local = parent.frame(), print. = TRUE, echo = TRUE, max.deparse.length = Inf, width.cutoff = max(20, getOption("width")), deparseCtrl = c("keepInteger", "showAttributes", "keepNA"), ...)
withCallingHandlers(expr, ...)
within(data, expr, keepAttrs = TRUE, ...)
withRestarts(expr, ...)
withVisible(x)
write(x, file = "data", ncolumns = if(is.character(x)) 1 else 5, append = FALSE, sep = " ")
write.dcf(x, file = "", append = FALSE, useBytes = FALSE, indent = 0.1 * getOption("width"), width = 0.9 * getOption("width"), keep.white = NULL)
writeBin(object, con, size = NA_integer_, endian = .Platform$endian, useBytes = FALSE)
writeChar(object, con, nchars = nchar(object, type = "chars"), eos = "", useBytes = FALSE)
writeLines(text, con = stdout(), sep = "\n", useBytes = FALSE)
xor(x, y)
xtfrm(x)
xzfile(description, open = "", encoding = getOption("encoding"), compression = 6)
zapsmall(x, digits = getOption("digits"))
