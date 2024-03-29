-- PostgreSQL 14
--! Data Types
-- https://www.postgresql.org/docs/current/datatype.html
bigint int8
bigserial serial8
bit
bit varying varbit
boolean bool
box
bytea
character char
character varying varchar
cidr
circle
date
double precision float8
inet
integer int int4
interval
json
jsonb
line
lseg
macaddr
macaddr8
money
numeric decimal
path
pg_lsn
point
polygon
real float4
smallint int2
smallserial serial2
serial serial4
text
time timetz
timestamp timestamptz
tsquery
tsvector
txid_snapshot
uuid
xml

--! keywords
-- https://www.postgresql.org/docs/current/sql-keywords-appendix.html
A
ABORT
ABS
ABSENT
ABSOLUTE
ACCESS
ACCORDING
ACOS
ACTION
ADA
ADD
ADMIN
AFTER
AGGREGATE
ALL
ALLOCATE
ALSO
ALTER
ALWAYS
ANALYSE
ANALYZE
AND
ANY
ARE
ARRAY
ARRAY_AGG
ARRAY_MAX_CARDINALITY
AS
ASC
ASENSITIVE
ASIN
ASSERTION
ASSIGNMENT
ASYMMETRIC
AT
ATAN
ATOMIC
ATTACH
ATTRIBUTE
ATTRIBUTES
AUTHORIZATION
AVG
BACKWARD
BASE64
BEFORE
BEGIN
BEGIN_FRAME
BEGIN_PARTITION
BERNOULLI
BETWEEN
BIGINT
BINARY
BIT
BIT_LENGTH
BLOB
BLOCKED
BOM
BOOLEAN
BOTH
BREADTH
BY
C
CACHE
CALL
CALLED
CARDINALITY
CASCADE
CASCADED
CASE
CAST
CATALOG
CATALOG_NAME
CEIL
CEILING
CHAIN
CHAINING
CHAR
CHAR_LENGTH
CHARACTER
CHARACTER_LENGTH
CHARACTER_SET_CATALOG
CHARACTER_SET_NAME
CHARACTER_SET_SCHEMA
CHARACTERISTICS
CHARACTERS
CHECK
CHECKPOINT
CLASS
CLASS_ORIGIN
CLASSIFIER
CLOB
CLOSE
CLUSTER
COALESCE
COBOL
COLLATE
COLLATION
COLLATION_CATALOG
COLLATION_NAME
COLLATION_SCHEMA
COLLECT
COLUMN
COLUMN_NAME
COLUMNS
COMMAND_FUNCTION
COMMAND_FUNCTION_CODE
COMMENT
COMMENTS
COMMIT
COMMITTED
CONCURRENTLY
CONDITION
CONDITION_NUMBER
CONDITIONAL
CONFIGURATION
CONFLICT
CONNECT
CONNECTION
CONNECTION_NAME
CONSTRAINT
CONSTRAINT_CATALOG
CONSTRAINT_NAME
CONSTRAINT_SCHEMA
CONSTRAINTS
CONSTRUCTOR
CONTAINS
CONTENT
CONTINUE
CONTROL
CONVERSION
CONVERT
COPY
CORR
CORRESPONDING
COS
COSH
COST
COUNT
COVAR_POP
COVAR_SAMP
CREATE
CROSS
CSV
CUBE
CUME_DIST
CURRENT
CURRENT_CATALOG
CURRENT_DATE
CURRENT_DEFAULT_TRANSFORM_GROUP
CURRENT_PATH
CURRENT_ROLE
CURRENT_ROW
CURRENT_SCHEMA
CURRENT_TIME
CURRENT_TIMESTAMP
CURRENT_TRANSFORM_GROUP_FOR_TYPE
CURRENT_USER
CURSOR
CURSOR_NAME
CYCLE
DATA
DATABASE
DATALINK
DATE
DATETIME_INTERVAL_CODE
DATETIME_INTERVAL_PRECISION
DAY
DB
DEALLOCATE
DEC
DECFLOAT
DECIMAL
DECLARE
DEFAULT
DEFAULTS
DEFERRABLE
DEFERRED
DEFINE
DEFINED
DEFINER
DEGREE
DELETE
DELIMITER
DELIMITERS
DENSE_RANK
DEPENDS
DEPTH
DEREF
DERIVED
DESC
DESCRIBE
DESCRIPTOR
DETACH
DETERMINISTIC
DIAGNOSTICS
DICTIONARY
DISABLE
DISCARD
DISCONNECT
DISPATCH
DISTINCT
DLNEWCOPY
DLPREVIOUSCOPY
DLURLCOMPLETE
DLURLCOMPLETEONLY
DLURLCOMPLETEWRITE
DLURLPATH
DLURLPATHONLY
DLURLPATHWRITE
DLURLSCHEME
DLURLSERVER
DLVALUE
DO
DOCUMENT
DOMAIN
DOUBLE
DROP
DYNAMIC
DYNAMIC_FUNCTION
DYNAMIC_FUNCTION_CODE
EACH
ELEMENT
ELSE
EMPTY
ENABLE
ENCODING
ENCRYPTED
END
END_FRAME
END_PARTITION
END-EXEC
ENFORCED
ENUM
EQUALS
ERROR
ESCAPE
EVENT
EVERY
EXCEPT
EXCEPTION
EXCLUDE
EXCLUDING
EXCLUSIVE
EXEC
EXECUTE
EXISTS
EXP
EXPLAIN
EXPRESSION
EXTENSION
EXTERNAL
EXTRACT
FALSE
FAMILY
FETCH
FILE
FILTER
FINAL
FINISH
FIRST
FIRST_VALUE
FLAG
FLOAT
FLOOR
FOLLOWING
FOR
FORCE
FOREIGN
FORMAT
FORTRAN
FORWARD
FOUND
FRAME_ROW
FREE
FREEZE
FROM
FS
FULFILL
FULL
FUNCTION
FUNCTIONS
FUSION
G
GENERAL
GENERATED
GET
GLOBAL
GO
GOTO
GRANT
GRANTED
GREATEST
GROUP
GROUPING
GROUPS
HANDLER
HAVING
HEADER
HEX
HIERARCHY
HOLD
HOUR
ID
IDENTITY
IF
IGNORE
ILIKE
IMMEDIATE
IMMEDIATELY
IMMUTABLE
IMPLEMENTATION
IMPLICIT
IMPORT
IN
INCLUDE
INCLUDING
INCREMENT
INDENT
INDEX
INDEXES
INDICATOR
INHERIT
INHERITS
INITIAL
INITIALLY
INLINE
INNER
INOUT
INPUT
INSENSITIVE
INSERT
INSTANCE
INSTANTIABLE
INSTEAD
INT
INTEGER
INTEGRITY
INTERSECT
INTERSECTION
INTERVAL
INTO
INVOKER
IS
ISNULL
ISOLATION
JOIN
JSON
JSON_ARRAY
JSON_ARRAYAGG
JSON_EXISTS
JSON_OBJECT
JSON_OBJECTAGG
JSON_QUERY
JSON_TABLE
JSON_TABLE_PRIMITIVE
JSON_VALUE
K
KEEP
KEY
KEY_MEMBER
KEY_TYPE
KEYS
LABEL
LAG
LANGUAGE
LARGE
LAST
LAST_VALUE
LATERAL
LEAD
LEADING
LEAKPROOF
LEAST
LEFT
LENGTH
LEVEL
LIBRARY
LIKE
LIKE_REGEX
LIMIT
LINK
LISTAGG
LISTEN
LN
LOAD
LOCAL
LOCALTIME
LOCALTIMESTAMP
LOCATION
LOCATOR
LOCK
LOCKED
LOG
LOG10
LOGGED
LOWER
M
MAP
MAPPING
MATCH
MATCH_NUMBER
MATCH_RECOGNIZE
MATCHED
MATCHES
MATERIALIZED
MAX
MAXVALUE
MEASURES
MEMBER
MERGE
MESSAGE_LENGTH
MESSAGE_OCTET_LENGTH
MESSAGE_TEXT
METHOD
MIN
MINUTE
MINVALUE
MOD
MODE
MODIFIES
MODULE
MONTH
MORE
MOVE
MULTISET
MUMPS
NAME
NAMES
NAMESPACE
NATIONAL
NATURAL
NCHAR
NCLOB
NESTED
NESTING
NEW
NEXT
NFC
NFD
NFKC
NFKD
NIL
NO
NONE
NORMALIZE
NORMALIZED
NOT
NOTHING
NOTIFY
NOTNULL
NOWAIT
NTH_VALUE
NTILE
NULL
NULLABLE
NULLIF
NULLS
NUMBER
NUMERIC
OBJECT
OCCURRENCES_REGEX
OCTET_LENGTH
OCTETS
OF
OFF
OFFSET
OIDS
OLD
OMIT
ON
ONE
ONLY
OPEN
OPERATOR
OPTION
OPTIONS
OR
ORDER
ORDERING
ORDINALITY
OTHERS
OUT
OUTER
OUTPUT
OVER
OVERFLOW
OVERLAPS
OVERLAY
OVERRIDING
OWNED
OWNER
P
PAD
PARALLEL
PARAMETER
PARAMETER_MODE
PARAMETER_NAME
PARAMETER_ORDINAL_POSITION
PARAMETER_SPECIFIC_CATALOG
PARAMETER_SPECIFIC_NAME
PARAMETER_SPECIFIC_SCHEMA
PARSER
PARTIAL
PARTITION
PASCAL
PASS
PASSING
PASSTHROUGH
PASSWORD
PAST
PATH
PATTERN
PER
PERCENT
PERCENT_RANK
PERCENTILE_CONT
PERCENTILE_DISC
PERIOD
PERMISSION
PERMUTE
PLACING
PLAN
PLANS
PLI
POLICY
PORTION
POSITION
POSITION_REGEX
POWER
PRECEDES
PRECEDING
PRECISION
PREPARE
PREPARED
PRESERVE
PRIMARY
PRIOR
PRIVATE
PRIVILEGES
PROCEDURAL
PROCEDURE
PROCEDURES
PROGRAM
PRUNE
PTF
PUBLIC
PUBLICATION
QUOTE
QUOTES
RANGE
RANK
READ
READS
REAL
REASSIGN
RECHECK
RECOVERY
RECURSIVE
REF
REFERENCES
REFERENCING
REFRESH
REGR_AVGX
REGR_AVGY
REGR_COUNT
REGR_INTERCEPT
REGR_R2
REGR_SLOPE
REGR_SXX
REGR_SXY
REGR_SYY
REINDEX
RELATIVE
RELEASE
RENAME
REPEATABLE
REPLACE
REPLICA
REQUIRING
RESET
RESPECT
RESTART
RESTORE
RESTRICT
RESULT
RETURN
RETURNED_CARDINALITY
RETURNED_LENGTH
RETURNED_OCTET_LENGTH
RETURNED_SQLSTATE
RETURNING
RETURNS
REVOKE
RIGHT
ROLE
ROLLBACK
ROLLUP
ROUTINE
ROUTINE_CATALOG
ROUTINE_NAME
ROUTINE_SCHEMA
ROUTINES
ROW
ROW_COUNT
ROW_NUMBER
ROWS
RULE
RUNNING
SAVEPOINT
SCALAR
SCALE
SCHEMA
SCHEMA_NAME
SCHEMAS
SCOPE
SCOPE_CATALOG
SCOPE_NAME
SCOPE_SCHEMA
SCROLL
SEARCH
SECOND
SECTION
SECURITY
SEEK
SELECT
SELECTIVE
SELF
SENSITIVE
SEQUENCE
SEQUENCES
SERIALIZABLE
SERVER
SERVER_NAME
SESSION
SESSION_USER
SET
SETOF
SETS
SHARE
SHOW
SIMILAR
SIMPLE
SIN
SINH
SIZE
SKIP
SMALLINT
SNAPSHOT
SOME
SOURCE
SPACE
SPECIFIC
SPECIFIC_NAME
SPECIFICTYPE
SQL
SQLCODE
SQLERROR
SQLEXCEPTION
SQLSTATE
SQLWARNING
SQRT
STABLE
STANDALONE
START
STATE
STATEMENT
STATIC
STATISTICS
STDDEV_POP
STDDEV_SAMP
STDIN
STDOUT
STORAGE
STORED
STRICT
STRING
STRIP
STRUCTURE
STYLE
SUBCLASS_ORIGIN
SUBMULTISET
SUBSCRIPTION
SUBSET
SUBSTRING
SUBSTRING_REGEX
SUCCEEDS
SUM
SUPPORT
SYMMETRIC
SYSID
SYSTEM
SYSTEM_TIME
SYSTEM_USER
T
TABLE
TABLE_NAME
TABLES
TABLESAMPLE
TABLESPACE
TAN
TANH
TEMP
TEMPLATE
TEMPORARY
TEXT
THEN
THROUGH
TIES
TIME
TIMESTAMP
TIMEZONE_HOUR
TIMEZONE_MINUTE
TO
TOKEN
TOP_LEVEL_COUNT
TRAILING
TRANSACTION
TRANSACTION_ACTIVE
TRANSACTIONS_COMMITTED
TRANSACTIONS_ROLLED_BACK
TRANSFORM
TRANSFORMS
TRANSLATE
TRANSLATE_REGEX
TRANSLATION
TREAT
TRIGGER
TRIGGER_CATALOG
TRIGGER_NAME
TRIGGER_SCHEMA
TRIM
TRIM_ARRAY
TRUE
TRUNCATE
TRUSTED
TYPE
TYPES
UESCAPE
UNBOUNDED
UNCOMMITTED
UNCONDITIONAL
UNDER
UNENCRYPTED
UNION
UNIQUE
UNKNOWN
UNLINK
UNLISTEN
UNLOGGED
UNMATCHED
UNNAMED
UNNEST
UNTIL
UNTYPED
UPDATE
UPPER
URI
USAGE
USER
USER_DEFINED_TYPE_CATALOG
USER_DEFINED_TYPE_CODE
USER_DEFINED_TYPE_NAME
USER_DEFINED_TYPE_SCHEMA
USING
UTF16
UTF32
UTF8
VACUUM
VALID
VALIDATE
VALIDATOR
VALUE
VALUE_OF
VALUES
VAR_POP
VAR_SAMP
VARBINARY
VARCHAR
VARIADIC
VARYING
VERBOSE
VERSION
VERSIONING
VIEW
VIEWS
VOLATILE
WHEN
WHENEVER
WHERE
WHITESPACE
WIDTH_BUCKET
WINDOW
WITH
WITHIN
WITHOUT
WORK
WRAPPER
WRITE
XML
XMLAGG
XMLATTRIBUTES
XMLBINARY
XMLCAST
XMLCOMMENT
XMLCONCAT
XMLDECLARATION
XMLDOCUMENT
XMLELEMENT
XMLEXISTS
XMLFOREST
XMLITERATE
XMLNAMESPACES
XMLPARSE
XMLPI
XMLQUERY
XMLROOT
XMLSCHEMA
XMLSERIALIZE
XMLTABLE
XMLTEXT
XMLVALIDATE
YEAR
YES
ZONE
