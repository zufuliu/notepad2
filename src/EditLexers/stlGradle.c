#include "EditLexer.h"
#include "EditStyleX.h"

static KEYWORDLIST Keywords_Gradle = {{
//++Autogenerated -- start of section automatically generated
"abstract as assert break case catch class const continue def default do else enum extends false final finally for goto "
"if implements import in instanceof interface it native new non-sealed null "
"package parcelable permits println private protected public record return "
"sealed static strictfp super switch synchronized this threadsafe throw throws trait transient true try var volatile "
"while yield "

, // 1 types
"boolean byte char double float int long short void "

, // 2 unused
NULL

, // 3 class
"AbstractCollection AbstractList AbstractMap AbstractQueue AbstractSequentialList AbstractSet ActionBar Activity "
"AdapterView AlertDialog Application Array ArrayAdapter ArrayBlockingQueue ArrayDeque ArrayList Arrays "
"AtomicBoolean AtomicInteger AtomicLong AtomicReference AudioFormat AudioManager AudioRecord AudioTrack "
"Base64 BaseAdapter BigDecimal BigInteger Binder BitSet Bitmap Boolean "
"Buffer BufferedInputStream BufferedOutputStream BufferedReader BufferedWriter Build Bundle Button "
"Byte ByteArrayInputStream ByteArrayOutputStream ByteBuffer ByteOrder "
"Calendar Canvas CharArrayReader CharArrayWriter CharBuffer Character Charset CheckBox ChoiceFormat Class ClassLoader "
"Collections Collectors Color "
"ConcurrentHashMap ConcurrentLinkedDeque ConcurrentLinkedQueue Console Constructor Context ContextWrapper Copy "
"CountDownLatch Currency "
"DataInputStream DataOutputStream DatagramPacket DatagramSocket Date DateFormat DecimalFormat DeflaterOutputStream "
"Dialog Dictionary Display Double DoubleBuffer Drawable "
"EOFException EditText Enum EnumMap EnumSet Environment Error EventObject Exception "
"Field File FileDescriptor FileInputStream FileOutputStream FilePermission FileReader FileSystem FileWriter "
"FilterInputStream FilterOutputStream FilterReader FilterWriter Float FloatBuffer Format Formatter "
"GZIPInputStream GZIPOutputStream Gradle GregorianCalendar GridView "
"Handler HashMap HashSet Hashtable HttpClient HttpCookie HttpRequest HttpURLConnection "
"IOError IOException Image ImageButton ImageView Inet4Address Inet6Address InetAddress InetSocketAddress "
"InflaterInputStream InputStream InputStreamReader IntBuffer Integer Intent IntentFilter "
"JarEntry JarException JarFile JarInputStream JarOutputStream JavaCompile KeyEvent "
"LayoutInflater LinearLayout LinkedHashMap LinkedHashSet LinkedList ListView Locale Long LongBuffer Looper "
"MappedByteBuffer Matcher Math Matrix Message MessageFormat Method Modifier Module MotionEvent MulticastSocket "
"Notification Number NumberFormat Object ObjectInputStream ObjectOutputStream Optional OutputStream OutputStreamWriter "
"Package Paint Parcel Pattern PendingIntent PhantomReference PipedInputStream PipedOutputStream PipedReader PipedWriter "
"Point PointF PrintStream PrintWriter PriorityQueue Process ProcessBuilder ProgressBar Project Properties "
"RadioButton RadioGroup Random Reader Record Rect RectF Reference ReferenceQueue Region RelativeLayout RemoteException "
"Runtime RuntimeException "
"Scanner Script ScrollView SearchView SecurityManager SeekBar Semaphore ServerSocket Service ServiceLoader Settings "
"Short ShortBuffer SimpleDateFormat Socket SocketAddress SoftReference SourceSet Spinner "
"Stack StackView String StringBuffer StringBuilder StringJoiner StringReader StringTokenizer StringWriter System "
"TableLayout Task TextView Thread ThreadGroup ThreadLocal ThreadPoolExecutor Throwable TimeZone Timer TimerTask "
"Toast ToggleButton TreeMap TreeSet "
"URI URL URLConnection URLDecoder URLEncoder UUID Vector View ViewGroup Void WeakHashMap WeakReference Window Writer "
"ZipEntry ZipException ZipFile ZipInputStream ZipOutputStream "

, // 4 interface
"Adapter Annotation Appendable AutoCloseable BaseStream BlockingDeque BlockingQueue ByteChannel "
"Callable Channel CharSequence Cloneable Closeable Collection Collector Comparable Comparator ConcurrentMap Condition "
"DataInput DataOutput Deque DoubleStream Enumeration EventListener Executor Flushable Formattable Function Future "
"HttpResponse IBinder IInterface IntStream Iterable Iterator List ListAdapter ListIterator Lock LongStream "
"Map MatchResult Menu MenuItem NavigableMap NavigableSet ObjectInput ObjectOutput OnClickListener "
"Parcelable Path Predicate Queue RandomAccess ReadWriteLock Readable Runnable "
"Serializable Set SortedMap SortedSet Spliterator Stream WebSocket "

, // 5 enumeration
"ElementType RetentionPolicy TimeUnit "

, // 6 constant
NULL

, // 7 annotation
"Basic Column Delegate DelegatesTo Deprecated Documented Entity FunctionalInterface Generated Id Inherited ManagedBean "
"Native NonEmpty NonNull OrderBy OrderColumn Override PostConstruct PreDestroy Priority "
"Readonly Repeatable Resource Resources Retention SafeVarargs Serial SuppressWarnings Table Target Transient Version "
"interface "

, // 8 function
"absoluteProjectPath actions addBuildListener addListener addProjectEvaluationListener afterEvaluate afterProject "
"allJava allSource allprojects allprojects^{} ant ant^{} application^{} apply artifacts artifacts^{} "
"beforeEvaluate beforeProject buildCache buildDir buildFile buildFinished buildscript buildscript^{} "
"caseSensitive childProjects classpath "
"compileClasspath compiledBy configurations configurations^{} configure container convention copy copySpec "
"defaultTasks delete dependencies dependencies^{} dependencyLocking dependsOn description destinationDir destroyables "
"didWork dirMode disableAutoTargetJvm distributions^{} doFirst doLast duplicatesStrategy "
"eachFile enabled evaluationDependsOn exclude excludes exec expand extensions "
"file fileMode fileTree files filesMatching filesNotMatching filter finalizedBy findProject findProperty from "
"getAllTasks getCompileTaskName getTaskName getTasksByName gradle gradleHomeDir gradleUserHomeDir gradleVersion group "
"hasProperty include includeBuild includeEmptyDirs includeFlat includedBuild includedBuilds includes inputs into "
"java java^{} javaexec localState logger logging manifest mkdir mustRunAfter name normalization "
"onlyIf options output outputs "
"parent path pluginManager plugins project projectDir projectsEvaluated projectsLoaded properties property publishing^{} "
"relativePath relativeProjectPath removeListener removeProjectEvaluationListener rename "
"reporting^{} repositories repositories^{} resources resources^{} rootDir rootProject runtimeClasspath "
"setProperty settings settingsDir settingsEvaluated shouldRunAfter signing^{} source sourceCompatibility sourceSets^{} "
"startParameter state status subprojects subprojects^{} sync "
"tarTree targetCompatibility task taskDependencies taskGraph tasks temporaryDir uri useLogger version with zipTree "

, // 9 GroovyDoc
"author code deprecated docRoot end exception exclude hidden hide highlight index inheritDoc link linkplain literal "
"param provides replace return see serial serialData serialField since snippet spec start summary systemProperty throws "
"uses value version "

, NULL, NULL, NULL, NULL, NULL
//--Autogenerated -- end of section automatically generated

, // 15 Code Snippet
"for^() if^() switch^() while^() catch^() else^if^() else^{} "
}};

static EDITSTYLE Styles_Gradle[] = {
	EDITSTYLE_DEFAULT,
	{ MULTI_STYLE(SCE_GROOVY_WORD, SCE_GROOVY_WORD_DEMOTED, 0, 0), NP2StyleX_Keyword, L"fore:#0000FF" },
	{ SCE_GROOVY_WORD2, NP2StyleX_TypeKeyword, L"fore:#0000FF" },
	{ SCE_GROOVY_ANNOTATION, NP2StyleX_Annotation, L"fore:#FF8000" },
	{ SCE_GROOVY_CLASS, NP2StyleX_Class, L"fore:#0080FF" },
	{ SCE_GROOVY_INTERFACE, NP2StyleX_Interface, L"bold; fore:#1E90FF" },
	{ SCE_GROOVY_TRAIT, NP2StyleX_Trait, L"bold; fore:#1E90FF" },
	{ SCE_GROOVY_ENUM, NP2StyleX_Enumeration, L"fore:#FF8000" },
	{ SCE_GROOVY_FUNCTION_DEFINITION, NP2StyleX_MethodDefinition, L"bold; fore:#A46000" },
	{ SCE_GROOVY_FUNCTION, NP2StyleX_Method, L"fore:#A46000" },
	{ SCE_GROOVY_ACTION, NP2StyleX_Action, L"fore:#FF8000" },
	{ SCE_GROOVY_PROPERTY, NP2StyleX_Property, L"fore:#648000" },
	{ SCE_GROOVY_CONSTANT, NP2StyleX_Constant, L"fore:#B000B0" },
	{ MULTI_STYLE(SCE_GROOVY_COMMENTLINE, SCE_GROOVY_COMMENTBLOCK, 0, 0), NP2StyleX_Comment, L"fore:#608060" },
	{ MULTI_STYLE(SCE_GROOVY_COMMENTLINEDOC, SCE_GROOVY_COMMENTBLOCKDOC, 0, 0), NP2StyleX_DocComment, L"fore:#408040" },
	{ MULTI_STYLE(SCE_GROOVY_COMMENTTAGAT, SCE_GROOVY_COMMENTTAGHTML, 0, 0), NP2StyleX_DocCommentTag, L"fore:#408080" },
	{ SCE_GROOVY_TASKMARKER, NP2StyleX_TaskMarker, L"bold; fore:#408080" },
	{ MULTI_STYLE(SCE_GROOVY_STRING_DQ, SCE_GROOVY_STRING_SQ, 0, 0), NP2StyleX_String, L"fore:#008000" },
	{ MULTI_STYLE(SCE_GROOVY_TRIPLE_STRING_DQ, SCE_GROOVY_TRIPLE_STRING_SQ, 0, 0), NP2StyleX_TripleQuotedString, L"fore:#F08000" },
	{ SCE_GROOVY_ESCAPECHAR, NP2StyleX_EscapeSequence, L"fore:#0080C0" },
	{ MULTI_STYLE(SCE_GROOVY_SLASHY_STRING, SCE_GROOVY_DOLLAR_SLASHY, 0, 0), NP2StyleX_Regex, L"fore:#006633; back:#FFF1A8; eolfilled" },
	{ SCE_GROOVY_ATTRIBUTE_AT, NP2StyleX_Attribute, L"fore:#FF4000" },
	{ SCE_GROOVY_LABEL, NP2StyleX_Label, L"back:#FFC040" },
	{ SCE_GROOVY_NUMBER, NP2StyleX_Number, L"fore:#FF0000" },
	{ SCE_GROOVY_VARIABLE, NP2StyleX_Variable, L"fore:#9E4D2A" },
	{ MULTI_STYLE(SCE_GROOVY_OPERATOR, SCE_GROOVY_OPERATOR2, SCE_GROOVY_OPERATOR_PF, 0), NP2StyleX_Operator, L"fore:#B000B0" },
};

EDITLEXER lexGradle = {
	SCLEX_GROOVY, NP2LEX_GRADLE,
//Settings++Autogenerated -- start of section automatically generated
		LexerAttr_AngleBracketGeneric,
		TAB_WIDTH_4, INDENT_WIDTH_4,
		(1 << 0) | (1 << 1), // class, method
		SCE_GROOVY_FUNCTION_DEFINITION,
		'\\', SCE_GROOVY_ESCAPECHAR, 0,
		0,
		0, 0,
		SCE_GROOVY_OPERATOR, SCE_GROOVY_OPERATOR2
		, KeywordAttr32(0, KeywordAttr_PreSorted) // keywords
		| KeywordAttr32(1, KeywordAttr_PreSorted) // types
		| KeywordAttr32(3, KeywordAttr_PreSorted) // class
		| KeywordAttr32(4, KeywordAttr_PreSorted) // interface
		| KeywordAttr32(5, KeywordAttr_PreSorted) // enumeration
		| KeywordAttr64(7, KeywordAttr_NoLexer | KeywordAttr_NoAutoComp) // annotation
		| KeywordAttr64(8, KeywordAttr_NoLexer) // function
		| KeywordAttr64(9, KeywordAttr_NoLexer | KeywordAttr_NoAutoComp) // GroovyDoc
		, SCE_GROOVY_TASKMARKER,
		SCE_GROOVY_STRING_DQ, SCE_GROOVY_ESCAPECHAR,
//Settings--Autogenerated -- end of section automatically generated
	EDITLEXER_HOLE(L"Gradle Build Script", Styles_Gradle),
	L"gradle",
	&Keywords_Gradle,
	Styles_Gradle
};
