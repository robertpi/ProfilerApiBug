- iLRewriteMap management needs to live outside RewriteMethod, so we can rewrite the same method twice
- JITCompilationStarted - needs to call RewriteMethod twice
- need to request rejit of a new method
- rejit needs to rewrite this method twice