pipeline {
    agent none

    options { checkoutToSubdirectory('cse-core') }

    stages {

        stage('Build') {
            parallel {
                stage('Build on Windows') {
                    agent { label 'windows' }
                    
                    environment {
                        CONAN_USER_HOME = "${env.BASE}\\conan-repositories\\${env.EXECUTOR_NUMBER}"
                        CONAN_USER_HOME_SHORT = "${env.CONAN_USER_HOME}"
                    }

                    stages {
                        stage('Conan add remote') {
                            steps {
                                bat 'conan remote add bincrafters https://api.bintray.com/conan/bincrafters/public-conan --force'
                            }
                        }
                        stage('Build Debug') {
                            steps {
                                dir('debug-build') {
                                    bat 'conan install ../cse-core -s build_type=Debug -b missing'
                                    bat 'cmake -DCMAKE_INSTALL_PREFIX=../install/debug -DCSECORE_USING_CONAN=TRUE -DCSECORE_BUILD_PRIVATE_APIDOC=ON -G "Visual Studio 15 2017 Win64" ../cse-core'
                                    bat 'cmake --build . --config Debug'
                                    bat 'cmake --build . --config Debug --target install'
                                }
                            }
                        }
                        stage('Build Release') {
                            steps {
                                dir('release-build') {
                                    bat 'conan install ../cse-core -s build_type=Release -b missing'
                                    bat 'cmake -DCMAKE_INSTALL_PREFIX=../install/release -DCSECORE_USING_CONAN=TRUE -DCSECORE_BUILD_PRIVATE_APIDOC=ON -G "Visual Studio 15 2017 Win64" ../cse-core'
                                    bat 'cmake --build . --config Release'
                                    bat 'cmake --build . --config Release --target install'
                                }
                            }
                        }
                        stage ('Test Debug') {
                            steps {
                                dir('debug-build') {
                                    bat 'ctest -C Debug -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                            post {
                                always{
                                    xunit (
                                        testTimeMargin: '30000',
                                        thresholdMode: 1,
                                        thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                        tools: [ CTest(pattern: 'debug-build/Testing/**/Test.xml') ]
                                    )
                                }
                                success {
                                    archiveArtifacts artifacts: 'install/debug/**/*',  fingerprint: true
                                }
                                cleanup {
                                    dir('debug-build/Testing') {
                                        deleteDir();
                                    }
                                }
                            }
                        }
                        stage ('Test Release') {
                            steps {
                                dir('release-build') {
                                    bat 'ctest -C Release -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                            post {
                                always{
                                    xunit (
                                        testTimeMargin: '30000',
                                        thresholdMode: 1,
                                        thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                        tools: [ CTest(pattern: 'release-build/Testing/**/Test.xml') ]
                                    )
                                }
                                success {
                                    archiveArtifacts artifacts: 'install/release/**/*',  fingerprint: true
                                }
                                cleanup {
                                    dir('release-build/Testing') {
                                        deleteDir();
                                    }
                                }
                            }
                        }
                    }
                }
                stage ( 'Build on Linux with Conan' ) {
                    agent { 
                        dockerfile { 
                            filename 'cse-core/Dockerfile.conan-build'
                            label 'linux && docker'
                            args "-v ${env.HOME}/jenkins_slave/conan-repositories/${env.EXECUTOR_NUMBER}:/root/.conan -e CONAN_USER_HOME_SHORT=None"
                        }
                    }
                    
                    stages {
                        stage('Conan add remote') {
                            steps {
                                sh 'conan remote add bincrafters https://api.bintray.com/conan/bincrafters/public-conan --force'
                            }
                        }
                        stage('Build Debug') {
                            steps {
                                dir('debug-build') {
                                    sh 'conan install ../cse-core -s compiler.libcxx=libstdc++11 -s build_type=Debug -b missing'
                                    sh 'cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../install -DCSECORE_USING_CONAN=TRUE -DCSECORE_BUILD_PRIVATE_APIDOC=ON ../cse-core'
                                    sh 'cmake --build .'
                                    sh 'cmake --build . --target install'
                                }
                            }
                        }
                        stage('Build Release') {
                            steps {
                                dir('release-build') {
                                    sh 'conan install ../cse-core -s compiler.libcxx=libstdc++11 -s build_type=Release -b missing'
                                    sh 'cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../install -DCSECORE_USING_CONAN=TRUE -DCSECORE_BUILD_PRIVATE_APIDOC=ON ../cse-core'
                                    sh 'cmake --build .'
                                    sh 'cmake --build . --target install'
                                }
                            }
                        }
                        stage ('Test Debug') {
                            steps {
                                dir('debug-build') {
                                    sh '. ./activate_run.sh && ctest -C Debug -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                        }
                        stage ('Test Release') {
                            steps {
                                dir('release-build') {
                                    sh '. ./activate_run.sh && ctest -C Release -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                        }
                    }
                    post {
                        always{
                            xunit (
                                testTimeMargin: '30000',
                                thresholdMode: 1,
                                thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                tools: [ CTest(pattern: '*-build/Testing/**/Test.xml') ]
                            )
                        }
                        success {
                            archiveArtifacts artifacts: 'install/**/*',  fingerprint: true
                        }
                        cleanup {
                            dir('debug-build/Testing') {
                                deleteDir();
                            }
                            dir('release-build/Testing') {
                                deleteDir();
                            }
                        }
                    }
                }
                stage ( 'Build on Linux with Docker' ) {
                    agent { 
                        dockerfile { 
                            filename 'cse-core/Dockerfile.build'
                            label 'linux && docker'
                        }
                    }

                    stages {
                        stage('Build Debug') {
                            steps {
                                dir('debug-build') {
                                    sh 'uname -a'
                                    sh 'cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=../install -DCSECORE_USING_CONAN=FALSE -DCSECORE_BUILD_PRIVATE_APIDOC=ON ../cse-core'
                                    sh 'cmake --build .'
                                    sh 'cmake --build . --target install'
                                }
                            }
                        }
                        stage('Build Release') {
                            steps {
                                dir('release-build ') {
                                    sh 'cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../install -DCSECORE_USING_CONAN=FALSE -DCSECORE_BUILD_PRIVATE_APIDOC=ON ../cse-core'
                                    sh 'cmake --build .'
                                    sh 'cmake --build . --target install'
                                }
                            }
                        }
                        stage ('Test Debug') {
                            steps {
                                dir('debug-build') {
                                    sh 'ctest -C Debug -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                        }
                        stage ('Test Release') {
                            steps {
                                dir('release-build') {
                                    sh 'ctest -C Release -T Test --no-compress-output --test-output-size-passed 307200 || true'
                                }
                            }
                        }
                    }
                    post {
                        always{
                            xunit (
                                testTimeMargin: '30000',
                                thresholdMode: 1,
                                thresholds: [ skipped(failureThreshold: '0'), failed(failureThreshold: '0') ],
                                tools: [ CTest(pattern: '*-build/Testing/**/Test.xml') ]
                            )
                        }
                        success {
                            archiveArtifacts artifacts: 'install/**/*',  fingerprint: true
                        }
                        cleanup {
                            dir('debug-build/Testing') {
                                deleteDir();
                            }
                            dir('release-build/Testing') {
                                deleteDir();
                            }
                        }
                    }
                }
            }
        }
    }
}
