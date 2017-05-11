{-
  SockeyeMain.hs: Sockeye

  Copyright (c) 2017, ETH Zurich.

  All rights reserved.

  This file is distributed under the terms in the attached LICENSE file.
  If you do not find this file, copies can be found by writing to:
  ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich,
  Attn: Systems Group.
-}

module Main where

import Control.Monad
import Data.List
import System.Console.GetOpt
import System.Exit
import System.Environment
import System.IO

import SockeyeAST as AST
import SockeyeParser
import SockeyeChecker
import qualified SockeyeBackendPrintAST as PrintAST

{- Compilation targets -}
data Target = None | PrintAST

{- Possible options for the Sockeye Compiler -}
data Options = Options { optInputFile :: FilePath
                       , optTarget    :: Target
                       }

{- Default options -}
defaultOptions :: Options
defaultOptions = Options { optInputFile = ""
                         , optTarget = None
                         }

{- Set the input file name -}
optSetInputFileName :: FilePath -> Options -> Options
optSetInputFileName f o = o { optInputFile = f }

{- Set the target -}
optSetTarget :: Target -> Options -> Options
optSetTarget t o = o { optTarget = t }

{- Prints usage information possibly with usage errors -}
usage :: [String] -> IO ()
usage errors = do
    prg <- getProgName
    let usageString = "Usage: " ++ prg ++ " [options] file\nOptions:"
        in hPutStrLn stderr $ usageInfo (concat errors ++ usageString) options

{- Setup option parser -}
options :: [OptDescr (Options -> IO Options)]
options = 
    [ Option "P" ["print"]
        (NoArg (\opts -> return $ optSetTarget PrintAST opts))
        "Print the AST to the console"
    , Option "h" ["help"]
        (NoArg (\_ -> do
                    usage []
                    exitWith ExitSuccess))
        "Show help"
    ]

{- evaluates the compiler options -}
compilerOpts :: [String] -> IO (Options)
compilerOpts argv =
    case getOpt Permute options argv of
        (actions, [f], []) -> liftM (optSetInputFileName f) $ foldl (>>=) (return defaultOptions) actions
        (actions, [], [])  -> do
            usage ["No input file\n"]
            exitWith $ ExitFailure 1
        (_, _, errors)     -> do
            usage errors
            exitWith $ ExitFailure 1

{- Runs the parser -}
parseFile :: FilePath -> IO (AST.NetSpec)
parseFile file = do
    src <- readFile file
    case parseSockeye file src of
        Left err -> do
            hPutStrLn stderr $ "Parse error at " ++ show err
            exitWith $ ExitFailure 2
        Right ast -> return ast

{- Runs the checker -}
checkAST :: AST.NetSpec -> IO ()
checkAST ast = do
    case checkSockeye ast of 
        [] -> return ()
        errors -> do
            hPutStrLn stderr $ intercalate "\n" (foldl flattenErrors ["Failed checks:"] errors)
            exitWith $ ExitFailure 3
        where flattenErrors es (key, errors)
                = let indented = map ((replicate 4 ' ') ++) errors
                  in es ++ case key of Nothing     -> errors
                                       Just nodeId -> ("In specification of node '" ++ show nodeId ++ "':"):indented

{- Runs the chosen backend -}
runBackend :: Target -> AST.NetSpec -> IO ()
runBackend None     = \_ -> return ()
runBackend PrintAST = PrintAST.compile

main = do
    args <- getArgs
    opts <- compilerOpts args
    let inFile = optInputFile opts
    ast <- parseFile inFile
    checkAST ast
    runBackend (optTarget opts) ast
    